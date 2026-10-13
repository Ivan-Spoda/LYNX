# Análise comportamental do FL Studio 2026: rodada 13 (REA) — FLEX (pacotes `.flexpack`), Kepler e geradores menores

> **Natureza deste documento.** Continuação de `REA_rodada12.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são descrições,
> layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais
> servem só para rastrear a evidência. **Os pacotes do FLEX são protegidos (conteúdo cifrado); o LYNX não
> contorna essa proteção** e este documento não descreve como fazê-lo (§1.3).

- **Alvos** (cópias locais em `tmp/r13/bin/`, originais em `Plugins/Fruity/Generators/<nome>/`):
  - `FLEX/engine_x64.dll` → `flexeng_x64.dll` (SHA-256 `bd6e8a4a…cdcc`, C++/MSVC, framework `d1framework`,
    leitor de pacotes, presets e zonas). `FLEX/FLEX_x64.dll` (`76acd4ab…3650`) é só a casca/IU; o motor v2
    (`engineV2_x64.dll`, presets `.flex2preset`) não foi decompilado.
  - `Kepler/engine_x64.dll` → `kepeng_x64.dll` (`baf37a1d…63ab`, C++/MSVC).
  - BassDrum, Toxic Biohazard, PoiZone, Wave Traveller: só formato + strings. BooBass e Drumpad já estão na
    rodada 10 (§5, §6); aqui só complementos.
- **Ferramentas:** Ghidra 12.1.4 *headless* em projetos próprios (`/tmp/r13gh_<dll>`), scripts em
  `tmp/r13/gh/scripts/` (`Decomp`, `StrRefs`, `Scalars` — busca de FourCC/constantes imediatas —, `Dis`).
  Coleta de `0xD5`: `tmp/r11/blobs.pkl` (`.flp` + `.fst`, `0xD5` após `0xC9`).
- **Scripts nossos (Python):** `tmp/r13/packls.py` (diretório do pacote). (Os scripts e cópias de conteúdo
  decifrado da análise foram apagados: ver §1.3.)
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/disassembly da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst`/pacotes |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

### Contagem nos dados (canais em `.flp` dos demos)

| Plugin (`0xC9`) | Canais em `.flp` | Total (`.flp`+`.fst`) | Tamanho do `0xD5` | Contêiner |
|---|---|---|---|---|
| **FLEX** | **57** | 195 | 2707 / 2694 / 2626 B | binário fixo: pacote + preset + 128 floats |
| Drumpad (+ "DrumPad") | 10 (+1) | 976 | 292/300 B | `SSLF` + `3PMD` (rodada 10 §6) |
| BassDrum | 5 | 10 (+52 "Bassdrum" em `.fst`) | 1384 B | `SSLF` + `1kiK` |
| Kepler | 4 | 285 | 178…189 B | nome de modo `Kepler-1` + floats |
| BooBass | 3 | 7 | 16 B | u32 + 3 × i32 (rodada 10 §5) |
| Toxic Biohazard | 3 | 7 | 10 050 B | `SSLF` + `TBi2` |
| Wave Traveller | 3 | 13 | 332 B…38 KB | u32 1000 + caminho do `.wav` + curva |
| PoiZone | 2 | 391 | 700 B | `SSLF` + `Ver4` |
| Kepler Exo | 0 | 192 | 793 B | `Kepler-2` |

---

## 1. FLEX — visão geral **[DECOMP+DADOS]**

**Resposta curta: o conteúdo dos pacotes (amostras e presets) é protegido pelo FLEX e o LYNX não o
extrai.** As amostras são conteúdo licenciado para uso dentro do FLEX; decifrá-las para tocar em outro
programa seria contornar a proteção, o que o projeto não faz. O que é aberto e utilizável: o **estado do
plugin** no `.flp` (nome do pacote, nome do preset e os 45 parâmetros globais, §1.1) e o **diretório** do
pacote (nomes das entradas, §1.2). Daí sai o *fallback*: escolher um patch do synth do LYNX pela categoria
deduzida dos nomes.

Dos 57 canais FLEX dos demos, **56 apontam para pacotes instalados** (o 57º usa "Synthesizer Science", que
não vem na instalação) **[DADOS]**:

| Pacote | Canais | Natureza dos presets usados |
|---|---|---|
| Melodic Techno | 18 | quase tudo **síntese** (osciladores internos); 1 preset usa 1 amostra |
| General Midi Library | 11 | **multi-amostra** (Piano Dark: 19 zonas; Flute: 9; Harp: 9…) |
| Arksun Cityscape | 11 | **síntese** (sem amostras no pacote) |
| Olbaid Compendium | 9 | **síntese** |
| Mobile Synth Pluck | 4 | multi-amostra (11 zonas cada) |
| Mobile Tuned 808 Bass | 2 | 1 amostra com laço |
| Core 808s (`.flex2pack`) | 1 | motor v2: 1 amostra por preset (`.usedsamples`) |
| Synthesizer Science | 1 | pacote ausente → *fallback* |

Ou seja: ~18 canais tocam como sampler multi-zona com fidelidade praticamente total; ~37 são presets de
síntese do FLEX (até 4 osciladores por zona), que precisam de mapeamento para o sintetizador do LYNX (§1.8).

### 1.1 Estado do plugin (`0xD5`, 2707/2694/2626 B) **[DADOS; DECOMP parcial]**

| Off | Tamanho | Conteúdo |
|---|---|---|
| 0x000 | 4 × u32 | `1`, `1`, `0x812`, `0` (cabeçalho fixo; `0x812` = bytes que seguem o nome do pacote) |
| 0x010 | 1024 | **nome do pacote** (ASCII, zero-terminado; ex.: `General Midi Library`) |
| 0x410 | 1 | 0 |
| 0x411 | 1024 | **nome do preset** (ASCII; ex.: `Piano Dark`) — corresponde à entrada `<nome>.flexpreset` (ou `.flex2preset`) do pacote |
| 0x811 | 0x2D | bloco de inteiros/bytes de estado (versões, índices de página da IU) — ignorar |
| 0x83E | u32 | **128** (número de floats que seguem) |
| 0x842 | 128 × f32 | **parâmetros globais do FLEX** (os 45 primeiros são os da tabela §1.6; o resto é 0) |
| 0xA42 | 0…0x51 | cauda (inteiros pequenos; varia com a versão: 2707 = 0x51 B, 2694 = 0x44 B, 2626 = 0) |

Os 45 floats do estado **sobrepõem** os 45 globais gravados no preset: no "Piano Dark" do demo
`NewStuff.flp`, `Env1 Attack Mod` = 0,742 no estado contra 0,5 no preset, e os macros 1–3 estão mexidos
(0,685/0,469/0,566) **[DADOS]**. O LYNX deve usar os valores do estado, não os do preset.

### 1.2 Contêiner do pacote (`.flexpack` / `.flex2pack`) **[DECOMP `FUN_18038b980`, `FUN_18038fff0` + DADOS]**

Inteiros **big-endian** no diretório; o conteúdo das entradas é little-endian depois de decifrado.

| Campo | Tipo | Conteúdo |
|---|---|---|
| versão | u32 BE | `0x00000002` (`.flexpack`) ou `0x01000002` (`.flex2pack`) |
| n | u32 BE | número de entradas |
| entrada × n | — | u32 BE `L` (tamanho do nome **incluindo** o zero) + `L` bytes de nome + **u64 BE offset absoluto** no arquivo + **u64 BE tamanho** |
| dados | — | as entradas, contíguas, logo após o diretório, na mesma ordem |

Entradas típicas: índice 0 `presetsIndex.presetsIndex`, índice 1 `pcf.pcf`, depois `<preset>.flexpreset`,
`<amostra>.flac`, e em `.flex2pack` pares `<preset>.flex2preset` + `<preset>.usedsamples`. Há entradas lixo
(`desktop.ini`). Em "Mobile Tuned 808 Bass" há **dois** `presetsIndex`/`pcf` (pacote concatenado). O `.ini`
ao lado do pacote só tem `[Ini] Version=N`.

### 1.3 Proteção do conteúdo

As entradas do pacote são cifradas pelo motor do FLEX e validadas ao abrir. **O LYNX não decifra os
pacotes** (conteúdo licenciado para uso no FLEX); por isso os detalhes do esquema não são documentados aqui,
e as seções 1.4–1.8 (formato interno dos presets, lido durante a análise) ficam só como referência de
comportamento, sem uso no import.

### 1.4 `presetsIndex` e `usedsamples` **[DADOS]**

`presetsIndex` decifrado: u32 versão (2), u32 n, depois n × (string nome, string categoria, string tags),
cada string = u32 LE tamanho (incluindo o zero) + bytes. Categorias = `typetags.txt` (Arpeggio, Bass, Bell,
Brass, Guitar, Keys, Lead, Drone, Drum, Organ, Pad, Piano, Pluck, Sequence, String, SFX, Wind); tags de
estilo = `styletags.txt` (Basic, Complex, Bright, Dark, Hard, Soft, Short, Long, …). Útil para o *fallback*.

`.usedsamples` (só `.flex2pack`): texto `"<amostra>.flac : <n>"` por linha — a lista de amostras do preset.

### 1.5 Preset `.flexpreset` (motor v1) **[DECOMP `FUN_1803f1300`, `FUN_1803c9aa0`, `FUN_180427bf0`, `FUN_18041f770` + DADOS]**

Tudo little-endian. "str" = u32 tamanho (incluindo o zero) + bytes.

| Ordem | Conteúdo |
|---|---|
| 1 | FourCC `flex` + FourCC `00su` (no arquivo aparecem como `xelf00su`) |
| 2 | u32 versão (< 36; vistos 0x11, 0x13, 0x23) |
| 3 | u32 N (44 ou 45) + N × f32: **parâmetros globais** (§1.6) |
| 4 | u32 G (≤ 16) + G **grupos**, cada um: FourCC `mgrp`, u32 versão (< 5), u32 P (= 300) + P × f32 (parâmetros do grupo), **u32 Z + Z zonas** (abaixo), depois blocos de modulação/efeitos (não decodificados) |
| 5 | blocos finais (mod matrix global, macros, nomes dos macros em strings: `FATDist`, `Cutoff`, `Space`…) |

**Zona** (`FUN_180427bf0`):

| Campo | Tipo | Conteúdo |
|---|---|---|
| marca | u8 | `0xFF` |
| versão | u8 | 2…8 (7 no GM, 8 no Melodic Techno) |
| nome | str | nome da zona (`GM Piano (6)`, `Empty Zone`…) |
| **rootKey** | i32 | nota-raiz MIDI (60 = C5 do FL) |
| **lowKey, highKey** | 2 × i32 | faixa de teclas (inclusiva) |
| **lowVel, highVel** | 2 × i32 | faixa de velocidade 0…127 |
| ? | i32 | 0 nos dados |
| (versão > 1) | 2 × i32 | 0 nos dados |
| S | u32 | número de *slots* de oscilador (4) |
| *slot* × S | — | ver abaixo |
| M | u32 | número de parâmetros da zona (746 ou 751; < 745 recebe defaults) |
| M × f32 | — | parâmetros da zona 0…1 (§1.7). **O índice no arquivo é o índice interno** usado por `setParam` |

**Slot** (`FUN_18041f770`), um por oscilador da zona:

| Campo | Tipo | Conteúdo |
|---|---|---|
| versão | u32 | 2 ou 3 |
| flag | u8 | 0 nos dados |
| **loopMode** | u32 | 0 = sem laço, 1 = laço contínuo (únicos valores vistos) |
| **sampleStart** | i64 | início da reprodução, em quadros |
| **loopStart** | i64 | início do laço, em quadros |
| **loopEnd** | i64 | fim do laço (ou fim da amostra quando `loopMode` = 0), em quadros; 0 = até o fim |
| arquivo | str | nome da amostra com extensão **`.wav`** (vazio = oscilador sintético). No pacote a entrada é o mesmo nome base com **`.flac`** |
| (versão ≥ 2) | u32 | 1 nos dados |
| (versão ≥ 3) | i64 | 0 nos dados |

Exemplo ("Piano Dark", GM): 19 zonas, uma amostra cada, faixas contíguas 0…127 (`GM Piano (10)` root 24
teclas 0–25; …; `GM Piano (8)` root 94 teclas 94–127), sem laço **[DADOS]**.

**Armadilha nos pontos de laço [DADOS]** (`tmp/r13/survey2.py`, 2070 *slots* com amostra): em 545 *slots*
do "General Midi Library" (todos com FLAC a 22 050 Hz) `loopEnd` é **maior que o comprimento do FLAC**,
quase sempre por um fator ≈ 2,0 (também 1,45, 1,5, 1,72…). As amostras foram reamostradas para o pacote e
os pontos ficaram na taxa original (44,1 kHz, 32 kHz…). Nos demais pacotes (44,1 kHz) tudo está dentro do
arquivo. Como o FLEX trata isso não foi lido no motor **[HIP]**; ver §Recomendações para a regra prática.

FLAC: 22 050 Hz (1407 *slots*), 44 100 Hz (456), 32 000 Hz (151) e taxas raras (20 279…96 000); 16 bits;
quase tudo mono. Sem blocos de laço/cue no FLAC (só STREAMINFO + VORBIS_COMMENT) **[DADOS]**.

### 1.6 Parâmetros globais (45) **[DECOMP `FUN_1803e96a0` (nomes), `FUN_1803eefc0` (aplicação) + DADOS]**

| i | Nome | i | Nome |
|---|---|---|---|
| 0–4 | Env1 Attack/Hold/Decay/Sustain/Release **Mod** | 23 | Master Filter Type |
| 5–9 | Env2 Attack/Hold/Decay/Sustain/Release **Mod** | 24–29 | Delay Type / Mix / Time / Feedback / Mod / Color |
| 10–17 | **Macro 1…8** | 30–34 | Reverb Mix / Decay / Size / Mod Depth / Color |
| 18 | Cutoff Mod | 35, 36 | Pre-Limiter Level, Post-Limiter Level |
| 19 | Env Amount Mod | 37, 38 | Delay Active, Reverb Active |
| 20 | Resonance Mod | 39 | **Transpose** (0,5 = 0) |
| 21 | **Master Filter Cutoff** | 40 | Global Filter Active |
| 22 | Master Filter Resonance | 41–44 | Limiter Active, Limiter LMH Mix, Limiter Type, Reverb Mod Speed |

Os "Mod" (0,5 = neutro) são deslocamentos relativos aplicados sobre os envelopes 1 e 2 de **todas** as
zonas; "Cutoff/Env Amount/Resonance Mod" idem para o filtro 1 das zonas. Escalas exatas não lidas **[HIP]**.

### 1.7 Parâmetros da zona (índices no arquivo) **[DECOMP `FUN_18041faf0` (registro de nomes, índice + 1) + DADOS]**

O registro de nomes usa `índice_do_arquivo + 1`; a correspondência foi confirmada nos dados (osciladores
desligados têm `Active` = 0 exatamente no índice previsto; o leitor força `Osc k Pitch` = 0,5 nos índices
43/107/171/235 em presets antigos). Blocos de oscilador com passo **64**, envelopes com passo **30**.

| Índice (arquivo) | Significado |
|---|---|
| 2 | tipo do filtro 1 (`round(x·127)`; 3 = 0,0236 é o mais comum) **[HIP]** |
| 3, 4, 5 | Filter 1 Cutoff, Resonance, Env Amt |
| 7, 8, 9 | Filter 2 Cutoff, Resonance, Env Amt |
| 17, 19 | Filter 1/2 Insert Amount |
| 33 + 64k | **Osc k Active** (k = 0…3) |
| 34 + 64k | **tipo do oscilador** `t = round(x·127)`: 0 Sample, 1 Waveform Dual + FM, 2 Noise, 3 WaveTable, 4 WaveTable Xfade, 5 Waveform Simple (padrão dos desligados), 6 WaveTable HQ, 7 Waveform Simple + FM (`FUN_1803e1df0`) |
| 39 + 64k | Osc k Transpose (Semi), 0,5 = 0 |
| 40 + 64k | Osc k Start Phase |
| 43 + 64k | Osc k Pitch (fino), 0,5 = 0 |
| 48 + 64k, 49 + 64k | Osc k Wave 1/2 Mix, PW |
| 50/51 + 64k | PW Amount 1/2 |
| 52…55 + 64k | FM vindo dos osciladores 1…4 |
| 78 + 64k | Osc k Sample Start |
| 86 / 88 + 64k | Wavetable Position / Morph Amount |
| 354 + 30e | **Env e+1** (e = 0…3): +0 Attack, +3 Hold, +5 Decay, +8 Sustain, +10 Release (+1, +2, +4… = curvas/flags) |
| 665…668 | Osc 1…4 Vol (0,879 = padrão) |
| 669…672 | Osc 1…4 Pan (0,5 = centro) |
| 673…676 | AM do osc 1 por osc 1…4 |
| 677, 678 | OSCs Volume, OSCs Pan |
| 743, 750 | Oscs Pitch, Oscs Pitch Semitone Quantized |

Env1 é o **envelope de amplitude** **[DADOS+HIP]**: no piano, A = 0, D = 0,5, S = 1, R = 0,425; na flauta,
A = 0,4, R = 0,25; no pad "PAD Minoo", A = 0,38, R = 0,45. Env2 é o envelope de modulação ligado ao
`Filter 1 Env Amt` nos presets de baixo/pluck (D 0,53…0,76, S baixo).

Escalas **[HIP, não lidas no motor]**: transposição ≈ `46·(2x − 1)` semitons (os valores 0,24 / 0,367 /
0,63, muito frequentes, dão −23,9 / −12,2 / +12,0 → arredondar para inteiro quando a distância for < 0,3);
tempos de envelope ≈ `t = 10·x³` s (curva cúbica típica do FL Mobile; validar); volume de oscilador com
0,879 ≈ 0 dB.

O tipo de forma de onda dentro de "Waveform Simple"/"Dual" e as tabelas de *wavetable* ficam em objetos
gravados depois dos parâmetros (`FUN_18041c970`, versões ≥ 5) — **não decodificados**.

### 1.8 Presets `.flex2preset` (motor v2, só "Core 808s"/"Core Synths") **[DADOS]**

Formato diferente (objetos com GUIDs de 32 hex, nomes de macro `Tone/Drive/Lo-Fi/Wobble/Chorus/Phaser/
Echo/Space`, zona `New Zone`). Não decodificado; a amostra usada sai do `.usedsamples` correspondente
(1 amostra por preset nos 808). Logo depois do nome da amostra há um inteiro 0x34 (52) **[HIP: nota-raiz]**;
os nomes `MSXII_808s_C_xx` indicam C.

---

## 2. Kepler (4 canais) **[DECOMP `FUN_18001f170`, `FUN_180016300`, tabela de nomes `FUN_18001fa30` + DADOS]**

Polifônico analógico estilo Juno (DCO com pulso/serra/sub, HPF, VCF, um ADSR, chorus I/II). O estado
começa com u32 0, u32 tamanho−12, u32 0, str nome do modo (`Kepler-1` = painel reduzido; `Kepler-2` =
modelo completo, usado pelo Kepler Exo). No modo `Kepler-1`:

| Off | Tipo | Conteúdo |
|---|---|---|
| 0x19 | u32 | versão (4) |
| 0x1D | u32 | n (34; 36 em versões novas) |
| 0x21 | n × f32 | controles do painel, 0…1, na ordem abaixo |
| depois | u8, f32, f32, u8, u8, u8 | flags (porta/legato?), valores 1,0 e 0,5, modo de qualidade **[HIP]** |

Ordem dos floats (o leitor converte cada posição para o índice interno com a tabela de `FUN_180016300`;
nomes resolvidos pela tabela de saltos de `FUN_18001fa30`):

| k | Controle | k | Controle |
|---|---|---|---|
| 0–4 | Arpeggiator Active / Mode / Range / Rate / Sync | 19, 20 | **Cutoff**, **Resonance** |
| 5–8 | LFO1 Rate / BPM Sync / Delay / Trig Mode | 21, 22 | Env1→Cutoff Polarity, **Env1→Cutoff Amount** |
| 9 | LFOs Trigger | 23, 24 | LFO1→Cutoff, Cutoff Key Follow |
| 10 | LFO1→DCO1 Amount (vibrato) | 25 | VCA Source (envelope/gate) |
| 11, 12 | DCO1 Pulse PW Amount, PW Mod Source | 26–29 | **Env1 Attack / Decay / Sustain / Release** |
| 13, 14 | DCO1 **Pulse Active**, **Saw Active** | 30 | Chorus Mode (0 off, I, II, I+II) |
| 15, 16 | DCO1 **Sub Osc Active**, Sub Osc Volume | 31 | Hold |
| 17 | Noise Osc Volume | 32 | Octave Transpose (0,5 = 0) |
| 18 | HP Freq | 33 | Main Volume |
| 34, 35 | LFO1 Retrig Phase Active / Phase (só n = 36) | | |

Exemplos: "Pads - Kepler" = pulso + serra, PW 0,25, LFO→DCO 0,19, cutoff 0,646, env→cutoff 0,335,
ADSR 0,3/0,5/0,5/0,317, chorus 0,66; "AceBass" = cutoff 0,105, env→cutoff 0,66, ADSR 0/0,352/0,196/0
**[DADOS]**. Escalas **[HIP]**: cutoff exponencial ~ `20·2^(10x)` Hz; tempos ~ `10·x³` s.

## 3. BassDrum (5 canais) **[STR+DADOS]**

`"SSLF"` + u32 0x560 + tag `1kiK` + nome do preset (campo fixo) + **caminho da amostra de ruído** (campo de
~512 B, caminho absoluto de Mac/Windows; o nome base existe em `BassDrum/Noise Samples/`) + **23 floats
0…1** a partir de ~0x50C. Os nomes de parâmetro no motor são exatamente 23: Base, Peak, Duration, Var. Peak,
Var. Slide, Slide Time, Var. Base, Click Freq, Click Amount, Var. Phase, Click Cut, Drive, Attack, Sampling
Ratio, Butt, Noise A/D/S, Noise Mix, Noise Filter, Noise Start, Noise Pitch, Output Level — a **ordem dos
floats não foi confirmada** (as strings estão embaralhadas pelo *pooling* do compilador) **[HIP: ordem
acima]**. LYNX: mesmo esquema do Fruit Kick (rodada 10 §4): seno com envelope de pitch Peak→Base em
Slide Time, decay = Duration, clique = noise curto (Click Amount), Drive → saturação se houver.

## 4. Wave Traveller (3 canais) **[DADOS]**

`0xD5`: u32 1000 (versão), u32 (tamanho do bloco de cabeçalho), u32 1 000 005, **u32 nota-raiz** (60…67),
u32 L + **caminho do `.wav` (L bytes, com `%FLStudioFactoryData%`)**, depois registros `tag = 2000` (88 ou
296 B) e ~36 KB da curva de *scratch*. Demos: `Legacy\Vocals\FLS_ThisDJisSoFunky.wav`,
`Vocals\VOC Lets Go.wav`, `Drums\Snares\808 Snare.wav`. LYNX: canal sampler com essa amostra
(`%FLStudioFactoryData%` → pasta de instalação do FL), raiz = campo 4; ignorar a curva de scratch (log).
Os presets de movimento são `Wave Traveller/Presets/*.spln` (Accelerate, Backwards, PingPong, Slowdown…).

## 5. Toxic Biohazard (3 canais) e PoiZone (2 canais) **[STR+DADOS]**

- **Toxic Biohazard:** `"SSLF"` + u32 0x273A + `TBi2`, 10 050 B fixos: ~2 KB iniciais de **formas de onda
  desenhadas** (só nos presets com onda própria), bloco de parâmetros por volta de 0x204C…0x22C0 e o nome
  do preset ("HiEnergy"). FM de 6 operadores (strings O1…O6 Coarse/Fine/Enable/Init/Dec/VelSens/Phase,
  Flt Cutoff/Res/EG, Master Atk/Dec/Sus/Rel, Lfo1/2). Não decodificado. LYNX: preset "FM genérico"
  (osc1 sine + osc2 sine +12 st com AM, ADSR 0/0,4/0,6/0,3) e log.
- **PoiZone:** `"SSLF"` + u32 0x2B4 + `Ver4` + ~47 floats 0…1 a partir de 0x0C, uma tabela de passos do
  *gate* (−1 = vazio) e o nome do preset em 0x27C (ex.: "ATMO Burst of Hopes"). Parâmetros (nomes do motor):
  OscA Shape/PW/Sync/Ring, OscB Shape/PW/Pitch/Detune, Osc/Noise Balance, EG A/D/S/R + Amount,
  Flt. Cutoff/Reso/KbdTrk/VelTrk, Amp. A/D/S/R, LFO Rate/Amount/Dest/Retrig, Unison, Glide, Delay, Chorus,
  Gate 1…16. Ordem não confirmada **[HIP]**. Mapeia bem para o sintetizador do LYNX (2 osc + ruído,
  2 ADSR, LP), mas exige ler a tabela de índices no motor (mesma técnica do Kepler §2).

## 6. BooBass e Drumpad (complemento à rodada 10)

Sem novidades de formato. Contagem atualizada: Drumpad 10 canais (+1 "DrumPad"), BooBass 3.

---

## Recomendações para o LYNX

1. **Despacho pelo `0xC9`:** "FLEX", "Kepler" (e "Kepler Exo" → *fallback*), "BassDrum"/"Bassdrum",
   "Wave Traveller", "Toxic Biohazard", "PoiZone". Continuar aplicando os eventos de canal (`0xDA`/`0xDB`).
2. **FLEX — fallback sem abrir os pacotes:** ler do `0xD5` o nome do pacote e do preset (§1.1) e escolher
   um patch do synth do LYNX pela categoria deduzida dos nomes (Bass → saw −1 oit + LP + decay curto; Pad →
   2 saws desafinados + attack lento; Pluck → saw + LP com envelope; Lead → saw + square; Keys/Bell/Piano →
   sine + triangle; 808/Kick → seno com queda de afinação), aplicando os globais do estado que forem
   independentes do preset (*Master Filter Cutoff*, *Transpose*) **[HIP]**, e registrar no relatório.
3. (removido: extração de amostras dos pacotes)
4. (removido: mapeamento dos presets internos, que exigiria abrir os pacotes)
5. **Kepler → sintetizador do LYNX:** osc1 `saw` (se Saw Active) com nível 1; osc2 `square` (se Pulse
   Active; PW ignorado salvo se o LYNX tiver PWM); osc3 `square` −1 oitava com nível = Sub Osc Volume (se
   Sub Active) — ou `noise` se Noise Osc Volume > Sub; cutoff/ressonância (§2); `filterEnv` = ADSR do Env1
   com amount `Env1→Cutoff Amount` (sinal pela Polarity); ADSR de amplitude = Env1 (ou *gate* se VCA Source
   = gate); LFO1 → pitch com amount `LFO1→DCO1` e → cutoff com `LFO1→Cutoff`; chorus → FX de chorus do
   canal; `Octave Transpose` → oitava dos osciladores. Arpeggiator Active → `ArpSettings` do canal.
6. **BassDrum / Wave Traveller / Toxic / PoiZone:** como §3–§5. Para o Wave Traveller, tocar a amostra
   (é o ganho mais barato e fiel); os outros três ficam em aproximação com log.
7. **Validação sugerida:** renderizar no FL "Piano Dark", "Harp" (laço fora do arquivo!), "Synpluck Cube",
   "808 Clean" e "BAS Lottunma" e comparar com o LYNX (afinação por zona, laço, envelope); medir a
   transposição de um oscilador com x = 0,24 para fixar a escala de semitons.

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| FLEX eng. `FUN_18038b980` | Abre pacote: extensões `.flexpack`/`.flex2pack`, entrada `presetsIndex.presetsIndex` | DECOMP |
| FLEX eng. `FUN_1803f1300` | Leitor do `.flexpreset` (`flex`/`00su`, versão, globais, grupos) | DECOMP |
| FLEX eng. `FUN_1803c9aa0` | Grupo `mgrp`: 300 parâmetros, zonas, blocos de modulação | DECOMP |
| FLEX eng. `FUN_180427bf0` | Zona: raiz, faixas, *slots*, parâmetros; conversões de versões antigas | DECOMP |
| FLEX eng. `FUN_18041f770` | *Slot*: modo de laço, início, laço, arquivo | DECOMP |
| FLEX eng. `FUN_1803e96a0`, `FUN_1803eefc0` | Nomes e aplicação dos 45 parâmetros globais | DECOMP |
| FLEX eng. `FUN_18041faf0` | Registro de nomes dos parâmetros da zona (índice + 1) | DECOMP |
| FLEX eng. `FUN_1803e1df0` | Nomes dos 8 tipos de oscilador | DECOMP |
| `tmp/r13/survey.py`, `survey2.py` | 2070 *slots*; laços × comprimento; taxas | DADOS |
| 195 blobs FLEX (`tmp/r11/blobs.pkl`) | Layout do `0xD5`; 56/57 pacotes presentes | DADOS |
| Kepler eng. `FUN_18001f170` | `Kepler-1`/`Kepler-2`: versão, n floats, flags finais | DECOMP |
| Kepler eng. `FUN_180016300` | Posição do painel → índice interno | DECOMP |
| Kepler eng. `FUN_18001fa30` (tabela de saltos `0x21934`/`0x216b8`) | Nomes dos 258 parâmetros | DECOMP |
| Strings dos motores BassDrum / Toxic / PoiZone | Nomes de parâmetros | STR |
| Blobs Wave Traveller / BassDrum / Toxic / PoiZone | Cabeçalhos, caminhos, nome do preset | DADOS |

### Questões em aberto

- FLEX: escalas reais (tempo dos envelopes, cutoff, transposição em semitons, volume); seleção da forma
  de onda em "Waveform Simple/Dual" e conteúdo dos objetos de *wavetable* (versão ≥ 5); como o motor lida
  com `loopEnd` além do fim do FLAC (reescala? *clamp*?); semântica dos 300 parâmetros do grupo e da
  matriz de modulação (macros → destinos); o 2º inteiro do *slot* (= 1) e o i64 da versão 3.
- FLEX v2 (`.flex2preset`, `engineV2_x64.dll`): formato de objetos com GUID, nota-raiz e laço.

- Kepler: escalas; significado dos 12 bytes finais; modelo `Kepler-2` (Exo, 0 canais nos demos).
- BassDrum/PoiZone: ordem dos floats (aplicar a técnica da tabela de saltos do Kepler aos motores deles).
- Toxic Biohazard: layout do bloco de parâmetros e das formas desenhadas.
