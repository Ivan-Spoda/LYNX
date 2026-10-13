# Análise comportamental do FL Studio 2026: rodada 9 (REA) — estado dos geradores baseados em sample

> **Natureza deste documento.** Continuação das rodadas 2–7 (a rodada 8, feita em paralelo, trata de
> Maximus/Soundgoodizer/Fast Dist/Filter/Flangus e não é tocada aqui). Engenharia reversa *clean-room*,
> apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são
> descrições, layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e
> globais servem só para rastrear a evidência.

- **Alvos** (todos em `Plugins/Fruity/Generators/<nome>/`):

  | Plugin | DLL (SHA-256) | Linguagem | `SaveRestoreState` (VMT/vtable + 3) |
  |---|---|---|---|
  | FPC | `5990885c…6f04` | Delphi (`TTestPlug`, VMT `0x868d48`) | `FUN_0086d4c0` |
  | DirectWave | `3432775d…6570` | C++ MSVC (`CDirectWaveFL`, vtable `0x1800d0418`) | `FUN_180016140` |
  | Fruity Slicer | `7616d1f8…2ed6` | Delphi (`TTestPlug`, VMT `0x766188`) | `FUN_00766c40` |
  | Slicex | `401b5ece…857b` | Delphi (`TTestPlug`, VMT `0x879d40`) | `FUN_0087a8a0` |
  | FL Keys / Drumpad | `56ca1209…822e` / `28a0a642…a361` | — | só dados (§5) |

  A VMT segue a ordem do SDK já usada nas rodadas 4 e 7 (3 = `SaveRestoreState`, 4 = `GetName`,
  6 = `ProcessParam`, 9 = `TriggerVoice`). Nas DLLs Delphi a VMT foi achada pelo nome da classe
  (`TTestPlug`, o ponteiro de nome fica em VMT−0x88); na DirectWave, pelo RTTI do MSVC
  (`.?AVCDirectWaveFL@@` → *Complete Object Locator* → vtable). Os formulários DFM foram lidos com o parser
  de `TPF0` da rodada 7. **Método:** o servidor REA estava ocupado pela rodada 8 (alvo único por sessão), então
  as decompilações foram feitas com o mesmo Ghidra 12.1.4 em modo *headless* num projeto próprio
  (scripts de decompilação/xref/grep em `tmp/r9/gh/`); endereços no espaço de imagem padrão de cada DLL.
- **Validação de formato:** 165 `.flp` de `FL Studio 2026/Data`, **seção de canais** (antes do 1º `0xEC`):
  `0xC9` (nome do plugin) → `0xD5` (blob). Contagens reais: **FPC 25** (20 arquivos), **DirectWave 57**
  (18 arquivos), **Slicex 43** (21 arquivos), **Fruity Slicer 10** (6 com o nome `"Fruity Slicer"` +
  4 com o nome antigo `"Fruity slicer"` — o importador precisa aceitar os dois), FL Keys 11, Drumpad 10
  (+1 `"DrumPad"`).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenções: tudo little-endian; `i32` com sinal; `P+0xNN` = offset no objeto do plugin; caminhos com
`%FLStudioFactoryData%` = raiz da instalação e `%FLStudioData%` = `<instalação>\Data` (ambos resolvidos
contra `/mnt/c/Program Files/Image-Line/FL Studio 2026` na validação).

### Formato de "chunks" comum a FPC e DirectWave **[DECOMP+DADOS]**

FPC e DirectWave usam o mesmo contêiner (escritor `FUN_0083c4a0` no FPC, `FUN_180003f70` na DirectWave;
leitor `FUN_0083c750` / `FUN_180003ea0`):

```
chunk = id:u32  tamanho:u64  corpo[tamanho]       (sem alinhamento; corpo pode conter sub-chunks)
```

O id de fim de lista é um chunk de tamanho 0 (`0x6A` para pad do FPC, `0x12E` para layer, `4` para zona da
DirectWave, `2` para programa da DirectWave). Um leitor genérico que só pula chunks desconhecidos lê os 82
blobs sem sobra de bytes **[DADOS]**.

---

## 1. FPC (25 canais) **[DECOMP+STR+DADOS]**

### 1.1 Layout do `0xD5`

| Off | Tipo | Conteúdo |
|---|---|---|
| 0x00 | u32 | **versão** `0x1E84E403` (os 25 blobs). O leitor recusa > `0x1E84E403`; se ≥ `0x200000` desloca 8 bits (→ 2.000.100); tamanhos de chunk em u64 a partir de 2.000.014 |
| 0x04 | chunks | sequência de chunks de topo (tabela abaixo) |

Chunks de topo (`FUN_0086d4c0`):

| id | Tamanho | Conteúdo |
|---|---|---|
| `2` | var. | **um pad** (contêiner, até 32; sempre 32 nos demos: pads 0–15 = banco A, 16–31 = banco B) |
| `1` | var. | nome do loop MIDI de bateria selecionado (UTF-16, ex. `World Loops\Polka\fpc_polka_10.mid`); ignorar |
| `3` | 1 | opção de UI (`P+0xC9C`) |
| `4` | 32 | estado de UI (2 × u32 em `+0xE74/+0xE78`); ignorar |
| `5` | 1 | opção de UI (`+0xB50`), só 3 demos |

Versões antigas (< 0xF424B) gravavam um contador de pads e um bloco por pad sem chunks
(`FUN_0086c5e0`); não aparecem nos demos.

### 1.2 Pad (chunk `2`, leitor/escritor `FUN_0086cc50`)

| id | Tamanho | Conteúdo |
|---|---|---|
| `0x64` | 64 | **16 × i32 de parâmetros do pad** (tabela 1.3) |
| `0x65` | var. | **nome do pad**, UTF-16LE sem terminador (vazio em pads sem sample) |
| `0x66` | 32 | 8 × i32; só o 1º é usado: **modo de disparo aleatório** (`pad+0x45`, tabela 1.5) |
| `0x69` | var. | **layer** (contêiner, 0..n; tabela 1.4) |
| `0x67` | 8 | índice do envelope que vem a seguir (0 = volume, 1 = pan) + 0 |
| `0x68` | var. | envelope serializado (§1.6) |
| `0x6A` | 0 | fim do pad |

### 1.3 Parâmetros do pad (`0x64`) **[DECOMP+STR+DADOS]**

A estrutura do pad fica em `P+0x14C + 0x56·i`. Mapeamento i32 → campo (lido em `FUN_0086cc50`; nomes
pelos *handlers* do DFM `WheelChange FUN_00857990`, `CutGroupChange FUN_0085f410`,
`OutputSelectorChange FUN_0085e490` e pelo `TriggerVoice FUN_008698a0`):

| i | Campo | Faixa / unidade | Demos (352 pads com sample) |
|---|---|---|---|
| 0 | **volume do pad** (`pad+2`) | 0…128 (`PadVolWheel` Max 127, default 127; 128 aparece nos pads vazios e em 51 pads usados). Ganho = `L11(v/127) = (11^(v/127) − 1)/10` **[DECOMP]** | 99 ×72, 128 ×51, 92, 64, 127… |
| 1 | **pan do pad** (`pad+6`) | −128…+128, 0 = centro | 0 ×257, ±4…±24 |
| 2 | **flags**: bit0 = **mute** (`pad+0`), bit1 = **solo** (`pad+1`), bit2 = `pad+0x26` **[HIP: "escalar volume com a velocity"]**, bit3 = **disparo aleatório/ciclo de layers sobrepostos** (`pad+0x44`) | — | 0 ×183, 8 ×112, 4 ×57 |
| 3 | **nota MIDI que dispara o pad** (`pad+0x22`) | 0…127 (mapa GM: 36 bumbo, 38 caixa, 42 chimbal fechado, 46 aberto…) | 36–51 típicos |
| 4 | **offset de saída do mixer** (`pad+0x1E`) | −99…103 (`PadOutputWheel`); 0 = mesma pista do canal | 0 ×130, 1…6 |
| 5 | **"Cut" (grupo que este pad corta)** (`pad+0x27`) | −1 (nenhum)…99 | −1 ×290, 0 ×58, 1 ×4 |
| 6 | **"Cut by" (grupo que corta este pad)** (`pad+0x2B`) | −1…99 | −1 ×291, 0 ×58, 1 ×3 |
| 7 | **afinação do pad** (`pad+0xA`) | −128…+128; cents = `v · 9,375` (= 1200/128, ±1 oitava) **[DECOMP]** | 0 ×345 |
| 8 | `pad+0x4E` | cor do pad em RGB **[HIP]** | 0 ou 0x47A145… |
| 9 | `pad+0x52` | **[HIP: índice de ícone/estilo]** | 0, 4…7, 239 |
| 10–15 | — | reservados (0) | — |

Disparo (`FUN_008698a0` → `FUN_0086b480`) **[DECOMP]**:
- A nota recebida dispara **todos** os pads cuja nota (`i=3`) é igual; pads mutados não tocam; se algum pad
  estiver em solo, só os em solo tocam. Validação: as **648 notas** de padrão dos canais FPC dos demos caem
  todas num pad com sample e nota igual, sem nenhuma nota ambígua (dois pads na mesma nota) **[DADOS]**.
- **A altura não segue a nota**: o FPC soma `(60 − nota)·100` cents ao pitch da voz, cancelando a
  transposição do host; o pad soa na altura original do sample + afinações (pad + layer).
- **Choke**: quando um pad P dispara, toda voz ativa de um pad Q com `cutBy(Q) = cut(P)` e `cutBy(Q) ≠ −1`
  é cortada (`P+0x173`/`P+0x177` em `FUN_0086b480`). Ex. típico: chimbal fechado com cut=0 e chimbal aberto
  com cut-by=0 (os 58 pares "0/0" dos demos).
- Ganho da voz = `L11(volPad/127)` (no grupo do pad) × `L11(volLayer/127)` (na voz da layer) **[DECOMP: as
  duas leis; HIP: a multiplicação simples]**. Pan = pan do pad + pan da layer, cada um em −128…128
  **[HIP: soma e limite]**.

### 1.4 Layer (chunk `0x69`, `FUN_0086c8f0`)

| id | Tamanho | Conteúdo |
|---|---|---|
| `0x12C` | var. | **caminho do sample**, UTF-16LE sem terminador (vazio = layer sem sample) |
| `0x12D` | 28 | 7 × i32 (abaixo) |
| `0x12E` | 0 | fim da layer |

| i | Campo (offset na layer) | Faixa / unidade | Demos (1.269 layers) |
|---|---|---|---|
| 0 | **volume da layer** (`+0x1C`) | 0…127 (DFM default 100; layer nova = 127). Ganho `L11(v/127)` | 127 ×1.112, 99 ×121 |
| 1 | **pan da layer** (`+0x20`) | −128…128 | 0 ×1.265 |
| 2 | **velocity mínima** (`+0x2C`) | 0…127, inclusiva | 0, 16, 32, 48, 64… |
| 3 | **velocity máxima** (`+0x30`) | 0…127, **exclusiva** (exceto 127, que inclui 127) | 127 ×464 |
| 4 | flags: bit0 → `+0x34` (sempre 1; copiado para a voz) **[HIP: "layer habilitada"]**; bit1 → `+0x3D` **[HIP: reverso, `SampleReverseBtn`]** | — | 1 ×1.269 |
| 5 | **afinação da layer** (`+0x38`) | −128…128 → `v·9,375` cents (somada à do pad) | 0 ×1.240; −54…+75 |
| 6 | **região do sample** (`+0x44`) | −1 = sample inteiro; ≥0 = índice da região/marcador do WAV (início/fim tirados da lista de regiões do arquivo) | −1 ×1.269 |

Seleção de layer na nota **[DECOMP]**: sem o bit3 do pad, **todas** as layers com `vmin ≤ vel < vmax`
tocam juntas. Com o bit3, só uma delas toca, escolhida pelo modo da tabela 1.5 entre as que cobrem a
velocity (`FUN_00869db0`, a última escolhida fica em `pad+0x49`). A velocity vem do host (0…1 × 128,
limitada a 0…127).

### 1.5 Modo de disparo de layers sobrepostas (`0x66`[0]) **[DECOMP+STR]**

| Valor | Menu (`RandomBtnMenu`) | Comportamento |
|---|---|---|
| 0 | Random | sorteio uniforme |
| 1 | Random (avoid previous) | sorteio entre as outras (padrão em pads novos; 240 pads) |
| 2 | Cycle backward | a anterior da lista, com volta |
| 3 | Cycle | a próxima da lista, com volta (os 112 pads "HQ Kit" de 8 layers) |

Os kits "HQ" dos demos têm 8 layers com faixas **sobrepostas** (0–32, 16–47, 32–64, 48–80, …, 111–127):
com o bit3 ligado e modo 3 isso vira *round-robin* dentro da faixa de velocity.

### 1.6 Envelopes do pad (`0x67`/`0x68`) **[DECOMP parcial + DADOS; semântica HIP]**

Dois por pad (0 = volume, 1 = pan), objetos `TFPCVolEnvelope`/`TFPCPanEnvelope`. Blob:
`u32 1, u32 3, u32 nPontos`, depois `nPontos × { f64 Δtempo (s) **[HIP]**, f64 valor 0…1, f32 tensão,
u32 tipo }` e um trailer de 9 × i32 (pontos de sustain/loop, −1 = nenhum, e 128,128,0,128). Demos: o de
pan é sempre o padrão (2 pontos); o de volume tem 2 pontos (1,0 → 0 em 1,25 s, tensão 0,2) em 283 pads e
4 pontos (0 → 1 em 0,75, → 0,5 em +1,5, → 0 em +0,25; sustain no ponto 2) em 377. Só há efeito quando o
objeto do envelope está "ativo" (`+0x48 ≠ 0`, testado em `FUN_0086b480`); qual campo do blob liga isso
não foi isolado **[HIP: o padrão é desligado — os pads soam como *one-shot* do sample inteiro]**.

### 1.7 Estatística e existência dos samples **[DADOS]**

- 25 blobs, todos versão 0x1E84E403, todos com 32 pads; **352 pads com sample**; layers por pad:
  1 ×185, 8 ×112, 4 ×24, 2 ×15, 3 ×9, 5 ×7.
- **1.269 caminhos, 1.269 existem** na instalação (todos `%FLStudioData%\Patches\Packs\Drums\…`).

---

## 2. DirectWave (57 canais) **[DECOMP+STR+DADOS]**

### 2.1 Layout do `0xD5`

`u32 versão` (38 ×49, 37 ×8; o leitor `FUN_18003dcd0` recusa > 38 e usa outro caminho para < 36) seguido
de chunks de topo (escritor `FUN_180040610`):

| id | Tamanho | Conteúdo |
|---|---|---|
| `6` | 16 | flags globais (u64): bits 0–2 opções, bit3 = "não registrado", bit4/bit5 = modo (`+0x90`) |
| `0x206` | 7 (5 em 6 blobs) | opções globais de UI/qualidade; ignorar |
| `5` | 628 | ×16: um por **canal MIDI/slot de multitimbralidade** (índice + nº do programa atribuído, ex. `0, 0x45` / `1, 0x3C`…) **[HIP]** |
| `1` | var. | **programa** (contêiner) ×128 — banco inteiro de 128 programas é sempre gravado |

**Só o programa 0 tem zonas** nos 57 blobs (os outros 127 são `"---"`, 2.819 B cada — por isso um blob de
"1 piano" tem ~380 KB) **[DADOS]**. O importador deve usar o programa selecionado do slot 0 (chunk `5`,
[HIP]) e, na prática, o primeiro programa com zonas.

### 2.2 Programa (chunk `1`, `FUN_180040c50` / leitor `FUN_18003f130`)

| id | Tamanho | Conteúdo |
|---|---|---|
| `0x64` | 30 | índice do programa (i32) + nº de zonas, polifonia, modo de voz etc. |
| `0x66` | var. | **nome do programa** (ASCII sem terminador; ex. `Close Grand`) |
| `0x67` | var. | nome do arquivo de origem (`Steinway Grand.dwp`), ASCII |
| `0x68`…`0x6B` | 10/18/17/17 | glide, LFOs e efeitos do programa (floats 0…1) |
| `0x6C` ×2, `0x6D` ×4 | 20 / 4 | moduladores/valores de modulação |
| `0x6E` ×100 | 13 | matriz de modulação (índice + fonte + destino + quantidade) |
| `3` | var. | **zona** (contêiner), uma por zona |
| `2` | 0 | fim do programa |

Modo de voz do programa: `Poly / Mono / Legato` e seleção de zonas sobrepostas `Normal / Cycle / Random /
Avoid prev.` são tabelas de nomes em `0x180107238…` **[STR]** (o campo exato no `0x64` não foi isolado).

### 2.3 Zona (chunk `3`, escritor `FUN_1800410d0`, leitor `FUN_18003f840`)

| id | Tamanho | Conteúdo |
|---|---|---|
| `0x1F4` | 25 | **cabeçalho da zona** (abaixo) |
| `0x1F5` | var. | nome da zona (ASCII, ex. `Close Grand 27`) |
| `0x1F6` | var. | **caminho do sample**, ASCII (`%FLStudioFactoryData%\Data\Patches\Packs\Instruments\Keyboard\Close Grand\Close Grand 27.wav`) |
| `0x1F7` | 40 | **formato e pontos do sample** (abaixo) |
| `0x1F8` | 8 | f32 **Pitch** (`Z+0x2EE`), u8 `Z+0x2BC`, u8 `Z+0x2BD`, u16 `Z+0x2BE` (=100) |
| `0x1F9` | 14 | u8 `Z+0x2C2` (modo de *time-stretch*), u8 `Z+0x2C3` (sync por ticks), 3 × f32 do *stretch* |
| `0x1FA` | 48 | 4 × f32: envios Delay/Chorus/Reverb/Dry da zona (0,5/0,5/0,35/1,0 típicos) + 32 B zerados |
| `0x1FB` ×2 | 20 | **filtro A e B**: u8 tipo (0 Off, 1 Lowpass, 2 Highpass, 3 Bandpass, 4 Notch, 5 Allpass **[STR]**), 3 bytes, f32 cutoff, f32 res, f32 forma, f32 |
| `0x1FC` | 2 | u8 `Z+0x2C0` (sincronizar ao tempo), u8 `Z+0x2C1` (fatiar pelo teclado) |
| `0x1FD` | 16 | **envelope de amplitude**: 4 × f32 A, D, S, R (`Z+0x2F2…0x2FE`) |
| `0x1FE`…`0x201` | 9 cada | 2 envelopes de modulação e 2 LFOs da zona |
| `0x202` ×2, `0x203` ×2 | 16 / 20 | parâmetros dos envelopes/LFOs de modulação |
| `0x204` ×16 | 8 | rotas de modulação da zona |
| `0x205` | var. | **áudio embutido, PCM cru** (opcional, §2.5) |
| `0x206` | var. | **áudio embutido comprimido em FLAC** (opcional; não aparece nos demos) |
| `4` | 0 | fim da zona |

**Cabeçalho `0x1F4` (25 B)** **[DECOMP+DADOS]**:

| Off | Tipo | Campo | Demos (1.287 zonas) |
|---|---|---|---|
| 0 | u8 | **nota raiz** (`Z+0x2B4`) | 36…96 |
| 1 | u8 | **tecla mínima** (inclusiva) | 0…127 |
| 2 | u8 | **tecla máxima** (inclusiva) | |
| 3 | u8 | **velocity mínima** | 0 em todas |
| 4 | u8 | **velocity máxima** | 127 em todas |
| 5, 6 | u8 | `Z+0x2B9` (grupo de zona/"key group" **[HIP]**), `Z+0x2BA` (grupo de saída **[HIP]**) | 0 |
| 7 | u8 | sem uso (lido e descartado; contém lixo 0/64/255) | |
| 8 | u8 | `Z+0x2C4` | 0 |
| 9 | f32 | **Amp Gain** 0…1 (`Z+0x302`) | 1,0 ×1.238; 0,675 ×49 |
| 13 | f32 | **Amp Pan** 0…1 (`Z+0x306`), 0,5 = centro | 0,5 |
| 17 | u32 | `Z+0x396` (0/1) — índice de alternância/rodízio **[HIP]** | 0 ×1.265, 1 ×22 |
| 21 | u32 | 512 (constante: quadros de folga do buffer, §2.5) | 512 |

**Formato `0x1F7` (40 B)**: `i64 quadros` · `i32 canais` · `i32 Z[2]` (nº de tempos/fatias usado por
"sync"/"fatiar pelo teclado"; 4…128) · `f32 taxa de amostragem` · `i32 modo de loop` · `i32 Loop A` ·
`i32 Loop B` · `i32 Sample Offset` · `i32 bits por amostra` (16/32). Pontos em quadros do sample.

**Modo de loop** (`Z[7]`, decisão no início/fim da voz em `FUN_180030090`) **[DECOMP+STR+DADOS]**:
0 = *Disabled* (toca uma vez, para no fim), 1 = *One-Shot* (toca até o fim mesmo após o note-off),
2 = *Forward* (loop A→B contínuo), 3 = *Sustained* (loop enquanto a nota está pressionada, depois segue até
o fim), 4 = *Bounce* (vai e volta). Demos: 0 ×907, **2 ×373**, 3 ×7. Zonas com loop: `LoopA > 0`
em 356, `LoopB > LoopA` em 380.

### 2.4 Conversões **[DECOMP]** (texto do parâmetro, `FUN_180027740`)

Os parâmetros da zona são floats normalizados 0…1 (o host vê `valor·100`, `FUN_180016070`; o nº do
parâmetro de host = `128·programa + índice`, nomes em `FUN_180029f30`).

| Parâmetro | Lei |
|---|---|
| Pitch | `cents = (v − 0,5)·2400` (±1 oitava); 0,5 = sem transposição |
| Amp Gain | `dB = 20·log10(v²)` → **amplitude linear = v²** (1,0 = 0 dB; 0,675 → −6,8 dB) |
| Amp Pan | `(v·100 − 50)` → "L/R"; pan −1…+1 = `2v − 1` |
| Amp Env Att / Dec / Rel | `t = 10·v⁴` s (0,5 → 0,625 s; 0,505 → 0,65 s; 0,25 → 0,039 s) |
| Amp Env Sus | `v·100` % |
| Smpl Offset / Loop A / Loop B | em quadros (`Z[10]`, `Z[8]`, `Z[9]`; o host vê `quadro/quadros`) |

Afinação por nota: taxa de leitura = `2^((nota − raiz)/12 + cents/1200) · taxa_do_sample / taxa_de_saída`
**[DECOMP: raiz e taxa; HIP: forma exata]**. Demos: envelope típico A=0, D=0,5, S=1,0, R=0,25…0,505
(piano: release 0,04…0,65 s); 90 zonas com D=1,0/S=0 (decai sem sustain).

### 2.5 Áudio embutido **[DECOMP+DADOS]**

Quando a opção de salvar o sample no projeto está ligada, cada zona ganha `0x205` com os canais **em
blocos separados (planar)**, cada bloco com `(quadros + 512) × bits/8` bytes (float32 quando bits = 32; o
`Z[2]=4` dessas zonas sugere "formato float" **[HIP]**). Ex.: 61.322 quadros × 2 canais × 4 B + 2×512×4 =
494.672 B. Alternativa `0x206` = o mesmo áudio como arquivo FLAC temporário embutido. Nos demos: **12 zonas
embutidas** (todas de um projeto, "Mehdi Ebrahiminejad – Never Out"), justamente as **12 cujo caminho
aponta para `D:\00 Projects\…` e não existe**.

### 2.6 Estatística **[DADOS]**

- 57 blobs, 57 programas com zonas (sempre o programa 0); zonas por programa: 6–12 (maioria), 17–18, 32,
  35, 45 ×3, 88 ×7. Total **1.287 zonas**, todas com `velocity 0–127` (sem camadas de velocity nos demos).
- **1.287 caminhos, 1.275 existem**; os 12 restantes têm áudio embutido.
- As **2.224 notas** de padrão dos canais DirectWave caem todas em exatamente uma zona (sem sobreposição de
  faixas de tecla) **[DADOS]**.

---

## 3. Fruity Slicer (10 canais) **[DECOMP+STR+DADOS]**

### 3.1 Layout do `0xD5` (`FUN_00766c40`, formato "stream" sequencial, sem chunks)

| Off | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | **versão** (15 ×4, 16 ×6; grava 16) |
| 0x04 | f32 | **comprimento original em tempos** (`LengthSelect`; 4, 8, 16) |
| 0x08 | f32 | **BPM original** (`TempoSelect`; 60, 100, 174, 175) |
| 0x0C | i32 | `P+0x128` **[HIP: pitch em cents, `PitchWheel` −1200…1200]** (0) |
| 0x10 | i32 | `P+0x12C` **[HIP: "fit length"/estiramento, `FGLengthWheel` −20000…20000]** (0, 83, 167, 766) |
| 0x14 | i32 | **método de time-stretch** `P+0x130` (0 Fill gaps, 1 Alt fill gaps, 2 Pro default, 3 Pro transient, 4 Transient, 5 Tonal, 6 Monophonic, 7 Speech **[STR]**; v<15 somava 2 se >1) |
| 0x18 | i32 | `P+0x134` **[HIP: fade in 0…100]** |
| 0x1C | i32 | `P+0x138` **[HIP: fade out 0…100]** |
| 0x20 | str8 | **caminho do sample**: u8 comprimento + bytes ANSI (ex. `%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav`) |
| … | u32 | **nº de fatias** N |
| … | N × fatia | `str8 nome` (pode ser vazio; `Kick`, `Snare`, `HiHat`…) · `i32 posição em amostras` · `i32 −1` · `f32 −1,0` · `u8 flags` (bit0 **[HIP: reverso, `InvertBtn`]**) |
| … | u8 | `P+0x141` (opção booleana de UI) |
| … | i32 | **nota da 1ª fatia** `P+0x144` (padrão 60) |
| … | u8 | **"play to end"** `P+0x13E` (1 = a fatia toca até o fim do sample; 0 = até a próxima fatia) **[DECOMP]** |
| … | i32 | taxa de amostragem do sample no momento de salvar (44100) **[HIP]** |
| … | u8 ×3 | `P+0x13F`, `P+0x13C`, `P+0x140` (auto-dump / declick / auto-fit **[HIP]**) |
| … | i32 | modo de exibição |
| … | 364 B | só v16: bloco de envelope/filtro da voz (`FUN_00757d10`) — ignorar |

Versões < 13 gravavam a fatia só com a posição (4 B) e, de 6 a 12, +1 byte de flag; < 9 gravavam os 3
floats do cabeçalho como 2.

### 3.2 Disparo (`TriggerVoice` `FUN_00768230`) **[DECOMP+DADOS]**

- Nota → fatia por uma tabela de 125 entradas (`P+0x198`); a fatia *i* fica na nota `nota1 + i`
  (`nota1` = `P+0x144` = 60 em todos os demos) **[DECOMP parcial; DADOS: as notas usadas nos padrões dos
  canais Slicer começam em 60 e vão até 60+N−1]**.
- A fatia toca **na altura original** (a voz recebe `(60 − nota)·100` cents, cancelando o host).
- Fim da fatia: início da próxima (ou o fim do sample se "play to end").

### 3.3 Estatística **[DADOS]**

10 blobs; fatias por canal: 9, 9, 18, 20, 22, 29, 32, 52 ×3; 9 dos 10 caminhos existem (o que falta é um
`.ogg` de um projeto de usuário). Posições em amostras a 44,1 kHz (ex. DL Breaker: 0, 22050, 44100…).

---

## 4. Slicex (43 canais) **[DECOMP+STR+DADOS]**

### 4.1 Layout do `0xD5` (`FUN_0087a8a0`, stream sequencial)

| Off | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | **versão**: `0x00060003` (35 blobs) ou `6` (8 blobs antigos, mesmo layout) |
| 0x04 | 8 × i32 | **parâmetros mestres** (índice = `Tag` do DFM): [0] env. do filtro, [1] cutoff, [2] **volume mestre** (0…16000, padrão 12800), [3] aleatoriedade (256), [4] LFO, [5] **pitch mestre em cents** (−1200…1200; demos 0, 455, 687, 760), [6] Mod X (256), [7] Mod Y (256) |
| 0x24 | 47 B | opções (`P+0x12F0`): u32 [0] = **modo de camadas** (0 All, 1 All gain-comp., 2 Velocity, 3 Mod X, 4 Mod Y, 5 Random, 6 Cycle, 7 Song-time **[DECOMP+STR]**), demais flags/seletores de UI |
| 0x53 | 3 × u32 | estado de UI (o 3º = oitava exibida no teclado, `BaseNoteSel`; só visual) |
| 0x5F | **deck A**, depois **deck B** | (abaixo) |
| fim | 8 × (20 B + 6 objetos) | grupos de articuladores (envelopes/LFOs); ignorar |

Cada **deck** (o Slicex tem dois, A e B; nos demos só o A tem fatias):

| Campo | Conteúdo |
|---|---|
| u8 | **0 = áudio embutido segue; 1 = só referência ao arquivo** |
| str8 | caminho do arquivo (u8 comprimento + ANSI; pode ser vazio) |
| (se 0) u32 + bytes | **WAV completo** (RIFF/WAVE) de `tamanho` bytes |
| 76 B | parâmetros do deck: 2 × f64 (−1 = automático), f64 início e f64 fim da região em **tempos**, 5 × i32, 6 × u8 |
| k objetos | envelopes/LFOs do deck (`FUN_00790720`) |
| u32 + N × 68 B | **parâmetros por região/fatia** (17 × i32; padrões do DFM: volume 8000 de 16000, pan 0 de ±6400, pitch 0 de ±1200 cents, cutoff 128, res 128, articuladores 12800, início 0, atraso 0) **[STR+DADOS; ordem exata dos campos HIP]** |

O áudio embutido é o WAV que o próprio Slicex/Edison gera: `fmt` (PCM 16 bits ou float 32), `data`,
`smpl` (nota raiz 60), `inst`, `acid` (tempos e BPM), `strc`, **`cue ` (um ponto por fatia, posição em
amostras)**, `LIST/adtl` com **`labl` (nome da fatia)** e `ltxt` (comprimento da região). Quando o deck só
referencia o arquivo, **os loops da instalação (`DL *.wav`) trazem exatamente os mesmos chunks** — ou seja,
**as fatias do Slicex vêm do WAV, não do blob**; o blob só guarda a contagem e os parâmetros de cada fatia
**[DADOS]**.

### 4.2 Mapeamento de teclas e altura **[DECOMP+DADOS]**

`TriggerVoice` (`FUN_00881d70`): a nota indexa uma lista de (deck, região) por tecla; se houver mais de uma
região na tecla, o modo de camadas escolhe. A voz soa na altura original + pitch da região + pitch mestre
(`(60 − nota)·100` cents cancelam o host). Nas notas de padrão dos 43 canais, as teclas usadas começam em
**60** e sobem (60…91) — **fatia *i* ↔ nota 60+i** **[DADOS; a tabela é derivada, não salva]**.

### 4.3 Estatística **[DADOS]**

- 43 blobs; **14 com WAV embutido** (o maior com 3,0 MB), **29 só com referência**; 8 na versão antiga 6.
- Fatias por deck A: 6…24 (DL loops: 19–24; legados: 8–11; "!westand out": 10–12); raiz sempre 60.
- Referências: todos os 29 arquivos existem. Embutidos: 11 dos 14 têm caminho que **não existe**
  (`!westand out.wav` ×8, `DL_The Funk.wav`, `DL_Ghost Break.wav`, um caminho da área de trabalho de um
  usuário) — o WAV embutido é a única fonte nesses casos; 2 apontam para arquivos que existem.
- 1 blob (`Video chopping.flp`) tem WAV embutido vazio (`data` de 0 bytes) e nenhum caminho.

---

## 5. FL Keys e Drumpad (baratos) **[DADOS+STR]**

**FL Keys** (11): `u32 1` · `str8 nome do conjunto` (`Grand Piano` ×8, `mda Piano` ×3) · 15 × i32
(−1000…1000; os 15 parâmetros do plugin: decay, release, hardness, vel→hardness, muffle, vel→muffle,
sensibilidade a velocity, estéreo, tune, detune, stretch, treble, pan/trêmolo, taxa do LFO, overdrive
**[STR, ordem; HIP: escala `p = (v + 1000)/2000`]**). O nome indexa
`Plugins/Fruity/Generators/FL Keys/Wavtables/<nome>.wti`, um INI com `[info]` (taxa, tipo `riff` ou `raw`,
pasta), `[velocitylayers]` (limiares de velocity) e um `[sampleN]` por amostra com `root` (nota base e
**início** da faixa), `high` (fim da faixa), `pos/end/loop` (para `.dat` cru). `Grand Piano` = 30 WAVs
(`CHANGPIANOHARD_A1.wav`…, raízes de 3 em 3 semitons, uma camada); `mda Piano` = 15 amostras num `.dat`
cru a 22.050 Hz **[HIP: PCM 16 bits mono]**; `Rhodes` = `.dat` cru a 32 kHz, 3 camadas. Ambos os conjuntos
usados nos demos existem.

**Drumpad** (10+1): blob `"SSLF"` + u32 tamanho + `"3PMD"` + nome do preset (`War Drum Lo 01`,
`Hi Bongo 01`, `Dance Kick 01`…) + ~60 floats. É **síntese por modelagem física, sem samples** — fora do
escopo do sampler; tratar como gerador não suportado (ou um bumbo/tom genérico).

---

## Recomendações para o LYNX

O LYNX tem: sampler de 1 sample por canal (pitch, ADSR, região início/fim, reverso, *cut*, fatias com
início fracionário e "nota 60+i toca a fatia i", filtro), canais roteados para pistas do mixer e padrões
com notas por canal. Com isso:

1. **Resolução de caminhos (comum).** `%FLStudioFactoryData%` → raiz da instalação do FL (ou pasta
   configurada pelo usuário); `%FLStudioData%` → `<raiz>\Data`; demais caminhos absolutos como estão; se o
   arquivo não existir, procurar pelo nome do arquivo na mesma pasta do `.flp`. Strings: FPC em UTF-16,
   DirectWave em ASCII, Slicer/Slicex em `str8` ANSI. Áudio embutido tem prioridade quando o arquivo não
   existe (DirectWave `0x205`, Slicex deck flag 0).

2. **FPC → um canal LYNX por pad com sample** (352 pads em 25 canais).
   - Para cada pad com ≥1 layer com caminho: criar um canal "*<nome do canal FPC> – <nome do pad>*" com o
     sample da **layer escolhida** (regra abaixo), mesma pista de mixer do canal FPC + `offset de saída`
     (i=4; limitar ao intervalo de pistas).
   - Nível do canal = `L11(volPad/127) · L11(volLayer/127)`; pan = `(panPad + panLayer)/128` limitado a ±1;
     pitch = `(tunePad + tuneLayer)·9,375` cents. **Sem key tracking**: o sampler deve tocar o sample na
     altura original qualquer que seja a nota (gerar as notas do novo canal sempre em 60, ou ligar uma opção
     "altura fixa" no canal).
   - **Divisão das notas:** cada nota do padrão do canal FPC vai para o(s) canal(is) cujo pad tem
     `nota == i3` (pads mutados descartados; se houver solo, só os em solo). Nos demos isso cobre 100 % das
     648 notas e nunca há dois pads na mesma nota.
   - **Velocity layers:** se o pad tem 1 layer, usar ela. Com várias e bit3 desligado, criar um canal por
     layer e mandar cada nota aos canais cuja faixa `[vmin, vmax)` contém a velocity (127 inclusivo). Com o
     bit3 ligado (kits HQ de 8 layers), o mais barato e fiel é **um canal com a layer que cobre a velocity
     média** (ou, se o LYNX ganhar *round-robin*, alternar entre as layers que cobrem a velocity — modo 3 =
     ciclo).
   - **Choke:** mapear `cut`/`cutBy` para o *cut* do LYNX: pads com `cutBy = g ≥ 0` entram no grupo de corte
     de quem tem `cut = g` (caso típico: chimbal aberto cortado pelo fechado). Se o *cut* do LYNX for
     "grupo simétrico", usar o mesmo número de grupo para os dois lados.
   - Envelope: ADSR "one-shot" (A=0, D/S irrelevantes, R curto) e região = sample inteiro (`i6 = −1` em
     todos os demos); reverso se bit1 da layer.

3. **DirectWave → um canal por zona** (ou, se o sampler ganhar *multi-zona*, um canal só).
   - Programa: o primeiro com zonas (sempre o 0 nos demos). Para cada zona: sample (arquivo ou `0x205`
     planar float32/int16 → WAV temporário), **nota raiz** (byte 0), **faixa de teclas** [b1, b2],
     **velocity** [b3, b4], pitch `(v−0,5)·2400` cents, ganho `v²`, pan `2v−1`, ADSR `t = 10·v⁴` s e
     sustain `v`, loop = modo `Z[7]` com A/B em quadros (Forward → loop contínuo; Sustained → loop até o
     note-off; Disabled/One-Shot → sem loop), início em `Sample Offset`.
   - **Divisão das notas:** cada nota do canal DirectWave vai para o canal da zona cuja faixa de tecla a
     contém (no máximo uma nos demos), **mantendo a nota** — esses canais *seguem* o teclado com pitch
     relativo à raiz: `semitons = nota − raiz`. Como o sampler do LYNX trata 60 como altura original, somar
     `(60 − raiz)·100` cents ao pitch do canal e manter a nota original no padrão.
   - Custo: os pianos dos demos têm 6–12 zonas por canal (88 em 7 canais); aceitável como canais ocultos
     agrupados, mas **um sampler multi-zona no LYNX** (lista de {sample, raiz, tecla lo/hi, vel lo/hi,
     ganho, pan, pitch, loop}) é o caminho certo e serve também para o FL Keys.

4. **Fruity Slicer → 1 canal com fatias.** Sample = caminho; fatias = `posição / nº de quadros do arquivo`
   (frações de início, na ordem gravada); nota 60+i = fatia i (se `nota1 ≠ 60`, transpor as notas do padrão
   por `60 − nota1`). Sem key tracking (a fatia toca na altura original). Fim = próxima fatia (ou o fim do
   sample se "play to end"). Pitch global `P+0x128` em cents **[HIP]**; ignorar time-stretch (BPM/tempos
   originais só são necessários se o LYNX ganhar estiramento).

5. **Slicex → 1 canal com fatias.** Fonte = WAV embutido (deck A, flag 0) ou arquivo referenciado; ler do
   WAV os `cue ` (posições → frações de início, ordenadas), `labl` (nomes), `smpl` (raiz 60) e `acid`
   (tempos/BPM, opcional). Nota 60+i = fatia i (confirmado pelos padrões). Volume do canal ← parâmetro
   mestre [2] (`12800` = 100 %, **[HIP] linear `v/12800`**), pitch ← mestre [5] em cents; parâmetros por
   fatia (volume/pan/pitch) só se o sampler ganhar valores por fatia. Deck B: ignorar (vazio nos demos).

6. **FL Keys → instrumento multi-zona a partir do `.wti`** (quando houver multi-zona; até lá, um canal com
   a amostra `.wti` mais próxima de C5 e key tracking normal). **Drumpad**: não suportado (síntese).

7. **Ordem de implementação sugerida (retorno/custo):** (a) FPC por pad (25 canais, 100 % dos samples
   existem, notas exatas); (b) Slicex via chunks do WAV (43 canais; extração de WAV embutido + leitor de
   `cue`/`labl`); (c) DirectWave como canais por zona e, depois, sampler multi-zona; (d) Fruity Slicer
   (trivial depois de (b)); (e) FL Keys via `.wti`.

---

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| FPC `FUN_0086d4c0` (VMT+3) | `SaveRestoreState`: versão, chunks de topo `1…5`, 32 pads | DECOMP |
| FPC `FUN_0086cc50` | Pad: chunks `0x64…0x6A`, mapeamento dos 16 i32 → campos | DECOMP |
| FPC `FUN_0086c8f0`, `FUN_00870430` | Layer: `0x12C/0x12D/0x12E`, 7 i32, defaults (vol 127, vel 0–127, região −1) | DECOMP |
| FPC `FUN_0083c4a0`, `FUN_0083c750` | Contêiner `id:u32 + tamanho:u64` | DECOMP |
| FPC `FUN_008698a0` (VMT+9), `FUN_0086b480` | Nota → pads (mute/solo), velocity → layer, leis L11, tune ×9,375, cut/cut-by, cancelamento da altura | DECOMP |
| FPC `FUN_00869db0` | Modos random / avoid previous / cycle back / cycle | DECOMP |
| FPC `FUN_00857990`, `FUN_0085c3b0/450/4d0`, `FUN_0085f410`, `FUN_0085e490`, `FUN_0086eb80` | Handlers do DFM → campos; texto "cents" (`v/128·1200`) | DECOMP |
| FPC DFM `TSynthEditorForm` @`0x5beb84` | Faixas de vol/pan/tune de pad e layer, cut/cut-by, output, menu de modos | STR |
| DirectWave RTTI `CDirectWaveFL` → vtable `0x1800d0418` | `SaveRestoreState` `FUN_180016140` | DECOMP |
| DirectWave `FUN_180040610`, `FUN_180040750`, `FUN_18003dcd0` | Topo: versão 38, chunks `6`, `0x206`, `5`×16, `1`×128 | DECOMP |
| DirectWave `FUN_180040c50`, `FUN_18003f130` | Programa: chunks `0x64…0x6E`, zonas | DECOMP |
| DirectWave `FUN_1800410d0`, `FUN_18003f840` | Zona: `0x1F4…0x206`, campos do cabeçalho e do formato, áudio planar + 512 | DECOMP |
| DirectWave `FUN_180026e10`, `FUN_180029f30`, `FUN_180027740`, `FUN_180028080/283d0/28ca0` | Parâmetro de host ↔ campo; leis de pitch/ganho/pan/tempo | DECOMP |
| DirectWave `FUN_180030090` | Início da voz: modo de loop `Z[7]`, sync, fatiar pelo teclado | DECOMP |
| DirectWave strings `0x105968…0x105a7c` | Tipos de filtro, modos de loop, modos de voz | STR |
| Fruity Slicer `FUN_00766c40` (VMT+3) | Layout v1…16 | DECOMP |
| Fruity Slicer `FUN_00768230` (VMT+9) | Tabela nota→fatia, play to end, altura fixa | DECOMP |
| Fruity Slicer DFM `TSynthEditorForm` | Tempo, comprimento, pitch, fades, métodos de stretch | STR |
| Slicex `FUN_0087a8a0` (VMT+3) | Layout v6/0x60003, 2 decks, WAV embutido, registros de 68 B | DECOMP |
| Slicex `FUN_00881d70` (VMT+9), `FUN_00896c10` | Nota → (deck, região), modos de camada; seletor de nota base só visual | DECOMP |
| Slicex DFM | Parâmetros mestres por `Tag`, defaults por região | STR |
| WAVs `DL *.wav` da instalação / embutidos | `cue`, `labl`, `ltxt`, `smpl`, `acid` = fatias | DADOS |
| FL Keys `Wavtables/*.wti` | Mapa de amostras (root/high/camadas) | DADOS+STR |
| 165 `.flp` (seção de canais + notas `0xE0`) | Contagens, versões, existência de arquivos, notas usadas | DADOS |

### Pendências

- FPC: qual bit/campo do blob do envelope (`0x68`) liga o envelope de volume/pan; confirmar bit2 do pad
  ("escalar volume com velocity") e bit1 da layer (reverso); lei de pan.
- DirectWave: campo do programa com o modo Poly/Mono/Legato e a escolha de zonas sobrepostas; significado de
  `Z+0x2B9/0x2BA` e `Z+0x396`; curva de cutoff dos filtros.
- Fruity Slicer: nomes exatos dos campos `P+0x128/0x12C/0x134/0x138` e dos 3 booleanos finais (ordem dos
  campos publicados do formulário).
- Slicex: ordem dos 17 i32 por região e unidade do volume mestre; papel do deck B.
- FL Keys: escala dos 15 parâmetros e formato do `.dat` cru.
