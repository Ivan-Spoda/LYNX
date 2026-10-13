# Análise comportamental do FL Studio 2026: rodada 12 (REA) — GMS, Ogun/Autogun e geradores restantes (DrumSynth Live, Sakura, Morphine)

> **Natureza deste documento.** Continuação de `REA_rodada10.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são descrições,
> layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais
> servem só para rastrear a evidência.

- **Alvos** (cópias locais em `tmp/r12/bin/`, originais em `Plugins/Fruity/Generators/<nome>/`):
  - `GMS/GMS_x64.dll` (SHA-256 `20c88eba…ec9c`, **C++/MSVC**, classes `GMSynth` / `CPluginRenderer`; formas de
    onda em `GMS/Synth Waves/*.wav`)
  - `Ogun/Ogun_x64.dll` (SHA-256 `c2c717df…e4c6`, Delphi, `TTestPlug`, VMT `0x8621f8`)
  - `Autogun`, `Fruity DrumSynth Live`, `Sakura`, `Morphine`: só formato + DFM/strings.
- **Ferramentas:** Ghidra 12.1.4 *headless* em projetos próprios (`/tmp/r12gh_<dll>`), scripts em
  `tmp/r12/gh/` (o REA compartilhado estava com o binário da outra rodada). Coleta: `tmp/r10/blobs.pkl`
  (`.flp` + `.fst`, `0xD5` após `0xC9`). Para o GMS também os 306 presets nativos `*.gmsynth` de
  `Data/Patches/Plugin presets/Generators/GMS/` (mesmo conteúdo do `0xD5`, sem o envelope SSLF).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/disassembly da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst`/`.gmsynth` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

### Contagem nos dados

| Plugin (`0xC9`) | Canais em `.flp` | Total (`.flp`+`.fst`) | Tamanho do `0xD5` | Contêiner |
|---|---|---|---|---|
| GMS | 10 | 15 (+306 `.gmsynth`) | 4968 B | `SSLF` + preset nativo `Syn4` |
| Ogun | 23 | 258 | 600 B…240 KB | u32 versão + zlib |
| Autogun | 9 | 13 | 2,4…172 KB | idêntico ao Ogun |
| Fruity DrumSynth Live | 9 | 15 | 19 467 B (v3/v4), 4232 B (v1) | binário próprio |
| Morphine | 7 | 11 | 47…342 KB | `SSLF` + chunks `Mor0` |
| Sakura | 5 | 510 | 596 B | `SSLF` + `Skr1` + 146 floats |

---

## 1. GMS (Groove Machine Synth) **[DECOMP+STR+DADOS]**

GMS é um subtrativo de 3 osciladores **wavetable de ciclo único** (cada oscilador carrega um `.wav` de
1024 amostras da pasta `Synth Waves`), ruído, modulação osc2→osc1 (sync / FM / AM), uníssono até 16 vozes,
filtro *ladder* 4 polos com saturação, ADSR de amplitude, 2 envelopes AD de modulação, 2 LFOs, um rack de
10 efeitos fixos e uma pequena matriz MIDI. Origem: FL Studio Groove / Mobile. Todos os parâmetros são
**floats 0…1**.

### 1.1 Contêiner e layout **[DECOMP+DADOS]**

`SaveRestoreState` (`FUN_1800c82f0`): o `0xD5` é `"SSLF"` + u32 `n` + `n` bytes; os `n` bytes são exatamente o
arquivo `.gmsynth` (o mesmo que o navegador de presets do plugin grava). Leitor do preset: `FUN_1800f4e60`;
escritor: `FUN_1800f43d0`.

| Off (no preset) | Tamanho | Conteúdo |
|---|---|---|
| 0x000 | 4 | tag de versão: `"4nyS"` (u32 `0x53796E34`, atual) ou `"3nyS"` (`0x53796E33`, antiga). `0x53344445` = variante "demo" (o leitor recusa se o plugin estiver em modo demo) |
| 0x004 | 7 × 512 | 7 *slots* de caminho (ASCII, zero-terminado). Só os 3 primeiros são usados: **forma de onda dos osciladores 1, 2, 3** (caminho absoluto do `.wav`, ex.: `…\GMS\Synth Waves\Saw Mean.wav`); os outros 4 vêm zerados |
| 0xE04 | 3 × i32 | **só em `Syn4`**: estado da UI (slot de FX selecionado, envelope selecionado 0/1, LFO selecionado 0/1). Ignorar |
| 0xE10 (`Syn3`: 0xE04) | 212 × f32 | **parâmetros 0…211** (§1.2), valor em 0…1 |
| +0x350 | 512 | nome do preset (ASCII; depois do zero vem lixo de memória — ignorar) |

Tamanhos: `Syn4` = 4 + 3584 + 12 + 848 + 512 = **4960** (+8 do SSLF = 4968 no `.flp`); `Syn3` = **4948**.
Nos dados: 206 presets `Syn3`, 115 `Syn4` (inclui os 15 de `.flp`/`.fst`) **[DADOS]**.

Detalhes do leitor **[DECOMP]**:
- O nome do arquivo de onda é resolvido pelo **nome base**: tenta o caminho gravado, depois a pasta
  `Synth Waves` do plugin e a pasta do usuário. `"Devils Horn"` é renomeado para `"Double Spike"` (arquivo
  atual). Para o LYNX basta o nome base sem extensão.
- O `.wav` é reamostrado para 1024 pontos e transformado em 512 tabelas limitadas em banda (FFT); a tabela
  usada depende da frequência da nota (`FUN_1800ef980`).
- Parâmetros ≥ 197 podem conter **lixo não inicializado** em presets antigos (ex.: "Basic Acid Bass" tem
  1,8e31 no 200). Sanear: valor não finito ou fora de [0,1] ⇒ 0 **[DADOS]**.

### 1.2 Índices dos parâmetros **[DECOMP]** (nomes de `FUN_1800f28f0`)

Pares par/ímpar = instância 1/2 (envelope 1/2, LFO 1/2).

| i | Nome interno | Default (`FUN_1800f1bd0`) |
|---|---|---|
| 0 | CHN: Mix Level | 0,75 |
| 1 | CHN: Panning | 0,5 |
| 2 | CHN: Pitch | 0,5 |
| 3 | CHN: Note Length | 0,444 (não usado no som) |
| 4, 5, 6, 7 | CHN: Amp Attack / Decay / Sustain / Release | 0 / 1 / 1 / 0,1 |
| 8 | CHN: Amp Level | 0,5 |
| 9 | CHN: Shuffle Enable | 0 |
| 10, 11 | CHN: Slide Time / Slide Enable | 0 / 0 |
| 12, 13, 14, 15 | Filter Cutoff / Resonance / KBD Tracking / Type | 1 / 0 / 0 / 0 |
| 16, 17 | Envelope 1/2 Amount | 0 |
| 18, 19 | Envelope 1/2 Invert | 0 |
| 20, 21 | Envelope 1/2 Attack | 0 |
| 22, 23 | Envelope 1/2 Decay | 1 |
| 24, 25 | Envelope 1/2 Destination | 0 |
| 26, 27 | LFO 1/2 Retrig | 0 |
| 28, 29 | LFO 1/2 Amount | 0 |
| 30, 31 | LFO 1/2 Invert | 0 |
| 32, 33 | LFO 1/2 Shape | 0 |
| 34, 35 | LFO 1/2 Rate | 0 |
| 36, 37 | LFO 1/2 Sync | 0 |
| 38, 39 | LFO 1/2 Destination | 0 |
| 40 | SYNTH: Mono Voice | 0 |
| 41 | SYNTH: Unison Voices | 0 |
| 42 | SYNTH: Unison Detune | 0,25 |
| 43 | SYNTH: Unison Stereo | 0 |
| 44 | SYNTH: Octave | 0,5 |
| 45 | SYNTH: Noise Mix | 0 |
| 46, 47 | SYNTH: Unison Phase / Unison Phase Retrig | 0 |
| 48, 49 | SYNTH: Mix Oscillator 2 / Mix Oscillator 3 | 0 |
| 50, 51 | SYNTH: Modulation / Mod Kind | 0 |
| 52, 53, 54 | Phase Offset Osc 2 / Phase Retrig Osc 2 / Osc 2 Invert | 0 |
| 55, 56 | Osc 1 Pitch / Osc 1 Detune | 0,5 |
| 57, 58 | Osc 2 Pitch / Osc 2 Detune | 0,5 |
| 59, 60 | Osc 3 Pitch / Osc 3 Detune | 0,5 |
| 61…116 | sem nome (valores 0/0,5/1 nos dados; não lidos pelo motor que analisamos) **[HIP: estado interno dos FX]** | |
| 117+k, 127+k | FX k "Param X" / "Param Y" (k = 0…9) | |
| 137+k | FX k LFO Rate | |
| 147+k | FX k "Glue" (o FX k fica ativo se ≠ 0) | |
| 157+k, 167+k, 177+k | FX k LFO Shape / LFO Sync / LFO Amount | |
| 187…196 | sem nome | |
| 197 | (sem uso no som) | |
| 198, 199 | **Velocidade →** quantidade (bipolar `2x−1`) / destino (inteiro, tabela abaixo) | |
| 200 | Velocidade → Amp Level (bool) | |
| 201, 202 | Fonte MIDI 2 → quantidade / destino (fonte `+0x3A14`) **[HIP: mod wheel]** | |
| 203, 204 | Fonte MIDI 3 → quantidade / destino (fonte `+0x3A08`) **[HIP: aftertouch]** | |
| 205 | Pitch-bend range: `int(12x)` semitons ("%d smt"; default 0,167 = 2) | |
| 206 | EQ de saída liga (bool) | |
| 207…211 | EQ de 5 bandas: ganho = `(x − 0,5)·24` dB | |

Os 10 FX fixos (`FUN_1800f2710`): 0 Distortion, 1 Crusher, 2 Flanger, 3 Phaser, 4 Low Pass, 5 Hi Pass,
6 Delay, 7 Reverb, 8 Panner, 9 Trancer; Param X/Y por FX: Distortion/Filter, Crush/Filter, Time/Feedback,
Depth/Feedback, Frequency/Bandwidth (×2), Feedback/Filter (×2), Panning/Binaural, Release/Depth **[DECOMP]**.

Destinos da matriz MIDI (198/201/203, `FUN_1800ef810`): 1 cutoff, 2 ressonância, 4 modulation, 6/10 env 1/2
attack, 7/11 env 1/2 decay, 8/12 env amount, 14/17 LFO 1/2 rate, 15/18 LFO 1/2 amount, 20 amp attack,
21 amp decay. A modulação soma `(2·quant − 1)·fonte` ao parâmetro e limita a [0,1] (`FUN_1800f50c0`). Nos
dados, 69 presets usam **velocidade → cutoff** (destino 1) **[DADOS]**.

### 1.3 Conversões para unidades reais **[DECOMP]**

Notação: `x` = valor 0…1 do parâmetro; "trunc" = truncamento para zero **em float32** (o plugin faz
`(int)(float)`; 0,16666667·72 − 36 dá −24 em float32 — emular em `float`, não em `double`).

**Afinação** (`FUN_1800f68f0`, `FUN_1800f5250`):
- Nota base em semitons MIDI: `nota + Δcanal + 12·oct`, com `Δcanal = −trunc(24 − 48·x2)` (±24 st) e
  **`oct = trunc(4·x44) − 2`** (−2…+2; x = 0,5 ⇒ 0). Dados: presets da pasta *Basses* concentram oct −1/−2 **[DADOS]**.
- Frequência: **`f = 220 Hz · 2^((nota − 57)/12)`** (nota MIDI; 57 = Lá 220 Hz), mais pitch-bend
  (`trunc(12·x205)` st × bend), mais modulação de pitch (abaixo).
- Osciladores k = 1, 2, 3: **semitons = trunc(72·x − 36)** (±36), **cents = trunc(100·x − 50)** (±50);
  razão = `2^((st + cents/100)/12)`.
- Portamento (Slide, 10/11): se ligado, a voz nova começa na altura da última nota; a altura persegue a
  nota com coeficiente por amostra `8,82/(T·fs)`, `T = 2·x10³ + 0,001` s ⇒ constante de tempo
  **τ = T/8,82** (chega a ~99 % em 4,6τ ≈ 0,52·T).
- Mono (40 ≠ 0): sempre reutiliza a voz 0 (mono com *legato* implícito se Slide ligado).

**Mistura** (`FUN_1800f68f0`): com `a = x48²`, `b = x49²`, `n = x45²`:
`g_osc1 = (1−a)(1−b)(1−n)`, `g_osc2 = a(1−b)(1−n)`, `g_osc3 = b(1−n)`, `g_ruído = n`. (É uma cascata de
*crossfades*: "Mix Osc 2" troca osc1 por osc2, "Mix Osc 3" troca o resultado por osc3, "Noise" troca tudo por
ruído branco uniforme ±0,5.) Osc2 Invert (54) inverte o sinal do osc2.

**Modulação osc2 → osc1** (`FUN_1800f7530`; Mod Kind = `trunc(2·x51)`; quantidade
`m = max(0, x50² + envs/LFOs com destino "mod")`):
- **0 = Sync:** osc1 roda a `razão1·(1 + 3·m)` e tem a fase zerada sempre que um "mestre" na razão1 natural
  completa um ciclo (*hard sync*; m = 0 ⇒ sem efeito).
- **1 = FM/PM:** a fase do osc1 recebe `osc2·15·m` (em quartos de ciclo **[HIP na escala exata]**).
- **2 = AM:** `osc1 × (1 − 3·m·osc2)`.
- Dados: sync 162, AM 112, FM 47 presets (m > 0 em 146) **[DADOS]**.

**Uníssono** (41–43): vozes `U = 1 + trunc(15·x41)` (1…16). Espalhamento total em oitavas
`s = (0,4·x42)²` (default 0,25 ⇒ 0,01 oct = 12 ct); a voz k (0…U−1) é multiplicada por
`2^((k/(U−1) − 0,5)·s)` ⇒ desafinação **±600·s centésimos** nos extremos. Pan das vozes alterna `±x43`
(com U ímpar a voz 0 fica no centro). Ganho de compensação `1/(1 + 0,15·(U − 1))`. Unison Phase (46) espalha
as fases iniciais (`k·4·x46` × unidade de fase **[HIP]**).

**Amplitude** (`FUN_1800f53e0`, `FUN_1800f58e0`, `FUN_1800f7530`): o envelope `e` é um seguidor
exponencial por amostra `e += (alvo − e)·c`, com `c = 8,82/(T·fs)`; **a saída é `e²`**.
- Attack: alvo 5,0 a partir de 0, `T_A = 50·x4³ + 0,001` s; passa ao decay quando `e ≥ 1` ⇒ **tempo real de
  ataque ≈ 0,0253·T_A = 1,265·x4³ s** (x4 = 1 ⇒ 1,27 s).
- Decay: alvo = sustain `S = x6`, `T_D = 50·x5⁴ + 0,001` s ⇒ τ = T_D/8,82 (x5 = 1 ⇒ τ = 5,7 s; default).
  **Nível de sustain na saída = S²**.
- Release: alvo 0, `T_R = 25·(0,55·x7 + 0,1)³ + 0,001` s, τ = T_R/8,82 (x7 = 0,1 ⇒ 11 ms; x7 = 1 ⇒ 0,30 s).
  Como a saída é `e²`, o nível cai a −40 dB em **≈ 2,3·τ**.
- Amp Level (8): ganho `10^((50·x8 − 25)/20)` (−25…+25 dB; 0,5 = 0 dB); se 200 ≠ 0, × velocidade.
- Saída do canal: Mix Level (0) `10^((80·x0 − 60)/20)` (0,75 = 0 dB; abaixo de −80 dB = mudo) e pan
  linear: `L = (1 − x1)·g`, `R = x1·g` (centro = −6 dB em cada lado), suavizado.

**Envelopes de modulação 1/2** (AD, sem sustain; x = parâmetro 20/21 e 22/23): attack com alvo 5 e
`T_A = 300·x⁵ + 0,001` s ⇒ **ataque real ≈ 7,59·x⁵ s**; decay até 0 com `T_D = 70·(0,8·x + 0,2)⁵ + 0,001`,
τ = T_D/8,82 (x = 0 ⇒ 2,5 ms; x = 1 ⇒ 7,9 s). Quantidade `A = x16²` (negativa se Invert). Destino
`trunc(3·x24)`: **0 cutoff, 1 modulation, 2 pitch, 3 "LFO"** (multiplica a saída do LFO de mesmo número:
vira um *fade-in/out* do LFO).

**LFOs 1/2** (`FUN_1800efb10`, `FUN_1800ef9f0`, `FUN_1800f5f00`):
- Forma `trunc(3·x32)`: **0 seno bipolar** `sin(2πφ)`, **1 triângulo unipolar 0…1**, **2 dente-de-serra
  descendente 1→0**, **3 quadrada 1/0**.
- Sync `trunc(2·x36)`: 0 = livre: **f = 25·(1 − x34)² + 0,01 Hz** (x = 0 ⇒ 25 Hz; x = 1 ⇒ 0,01 Hz);
  1 = sincronizado: período = `v` tempos com `v = [1/8, 1/4, 3/8, 1/2, 3/4, 1, 2, 4, 8][trunc(9·x34)]`
  (rótulos "1/8"…"8/1"); 2 = sincronizado ×4 (`4v` tempos) **[DECOMP; HIP na leitura "tempos" vs "compassos"]**.
- Retrig (26): zera a fase no note-on. Amount `x28²` (negativo se Invert).
- Destino `trunc(3·x38)`: **0 cutoff, 1 modulation, 2 pitch, 3 volume** (somado ao `e²` do amplificador).
- Validação: "Never Out — Disco Fuzz" tem LFO seno 5,9 Hz no pitch com ±0,28 st (= vibrato) **[DADOS]**.

**Escalas dos destinos** (`FUN_1800f68f0`, `FUN_1800f7530`): pitch: envelope **±24 st × A**, LFO
**±12 st × amount**; cutoff: soma direta na escala normalizada do corte (1,0 = 9,77 oitavas, abaixo);
modulation: soma a `x50²`.

**Filtro** (`FUN_1800f32f0`, tabela em `FUN_1800f0b20`): *ladder* de 4 polos de 1 polo cada, com
saturação racional na entrada e **4× sobreamostragem**. Para um corte normalizado `u` (= x12 + keytrack +
envelopes/LFOs, limitado a [0,1)): coeficiente `g = 0,0005·2^(9,7734·u)` (16 bits de tabela) e
realimentação `k = x13 · 0,7565·(5 + 7,5·g)` (≈ 3,8…6,3 · ressonância; auto-oscila perto de x13 ≈ 1).
Frequência equivalente (polo de 1ª ordem a 4·fs): **`fc ≈ −ln(1 − g)·4·fs/(2π)`** ⇒ a 44,1 kHz:
u = 0 → 14 Hz; 0,25 → 77 Hz; 0,5 → 420 Hz; 0,75 → 2,4 kHz; 1 → 16 kHz. A tabela não depende de `fs`
**[HIP: em 48 kHz tudo sobe ×1,09]**.
- Keytracking (14): `u += x14·(nota − 57)/128` (x14 = 1 ⇒ ~0,92 oitava por oitava).
- Tipo `trunc(2·x15)`: **0 passa-baixas 24 dB** (saída do 4º estágio), **1 passa-banda** (3º − 4º
  estágio), **2 "passa-altas"** (entrada saturada − 4º estágio) **[DECOMP para as fórmulas; HIP para os nomes]**.
  Dados: LP 283, tipo 1 em 32, tipo 2 em 6 **[DADOS]**.

### 1.4 As formas de onda (`Synth Waves`) **[DADOS]**

Análise harmônica (h1…h12 relativos ao maior) dos 29 `.wav` (script `tmp/r12/waves.py`):

| Onda | Perfil | LYNX |
|---|---|---|
| Sine, Keys, Smooth Sub | fundamental dominante (Smooth Sub: h2 0,8, h3 0,4, decai rápido) | `sine` (Smooth Sub: `triangle`) |
| Triangle | ímpares 1/k² | `triangle` |
| Square, Square Smooth, Square Retro | ímpares (Retro: ímpares ~0,1 constantes) | `square` |
| Saw, Saw Mean, Saw Retro | ~1/k | `saw` |
| Beast, Crunch, Electro, Grubby, Ogre, Troll, String, Rhodes, Dirt, Fox, Funky, Filth, Strat, Silk, Vocal, Throat | espectros ricos/formânticos (sem padrão ímpar) | `saw` (Rhodes, String: `triangle`) |
| **Digital** | só harmônicos pares (h1 ≈ 0) = quadrada uma oitava acima | `square` **+12 st** |
| **Double Spike** (ex-"Devils Horn"), Liquid | h2 dominante | `saw` **+12 st** |

Uso nos presets: Saw 289, Sine 153, Square 81, Saw Retro 56, Saw Mean 53, Smooth Sub 34…

### 1.5 Validação nos dados **[DADOS]**

Script `tmp/r12/gms_dec.py` (saída completa em `tmp/r12/gms_all.txt`). Exemplos coerentes com §1.3:
- "Basic Acid Bass": oct −1, cutoff 0 (14 Hz) + env 1 → cutoff 0,65 (≈ +6,3 oct ⇒ pico ~1,1 kHz) com
  τ = 0,42 s, ressonância 0,87 = acid clássico.
- "Never Out — Bass Impacter": env 1 → pitch, amount 1 (±24 st), decay τ = 12 ms = "queda" de kick.
- "Bell Pure Crystal": cutoff 0,205 + **velocidade → cutoff** +0,94: o brilho vem da velocidade.
- Mono em 82/321, uníssono ≥ 2 em 267/321, sustain cheio em 203/321.

---

## 2. Ogun e Autogun **[DECOMP+STR+DADOS]**

Ogun é um aditivo "de metais/sinos" (harmônicos gerados por *seed*, com decaimento por harmônico e
"articuladores" no estilo Sytrus). **Autogun é o mesmo motor e o mesmo formato** (a diferença é a UI que
sorteia o preset por número; o estado completo é gravado) **[DADOS: mesmos campos, mesmas versões]**.

### 2.1 Contêiner e layout (`SaveRestoreState` = `FUN_00864d00`, VMT+3)

| Off | Conteúdo |
|---|---|
| 0x0 | u32 versão. Se > 0xFFFF, a versão é a palavra alta (o FL grava `0x00130003` = v19 e `0x00120003` = v18; `.fst` antigos gravam 0…14 direto). ≥ 0x130004 é recusado |
| 0x4 | **stream zlib** com o resto |

Conteúdo descomprimido (v ≥ 14):

| Off | Conteúdo |
|---|---|
| 0x000 | **64 × i32** = parâmetros `P[0…63]` (internamente `P+0x12C + 4i`). Versões antigas gravam menos: 12 B de cabeçalho (8 se v < 8) + `0xF4` B reduzidos por versão (−1 se v < 1, −3 se v < 2, −1 se v < 7, −16 se v < 9, −1 se v < 11, −1 se v < 12, −6 se v < 13) |
| 0x100 | 3 bytes: Delay tempo-based, Delay enable, (?) |
| 0x103 | 3 × i32: tipo das 3 bandas do EQ (0 low shelf, 1 peaking, 2 high shelf) |
| 0x10F | comentário/nome (v ≥ 18: string; v < 18: u32 tamanho + bytes) — ex.: "Metal Drum\rAstes\rX/Y change timbre" |
| … | 13 articuladores (envelopes de pontos + LFO + mapas de teclado/velocidade/X/Y, mesmo objeto do Sytrus), depois mais articuladores de EQ e a tabela de timbre; blobs grandes (até 1,2 MB descomprimidos) **não foram decodificados** |

### 2.2 Parâmetros (Tag do DFM `TSynthEditorForm` = índice) **[STR+DECOMP]**

| i | Nome | Faixa (def.) | Conversão / observação |
|---|---|---|---|
| 0 | Master level | 0…256 (128) | ganho `v/256`? **[HIP: linear]** |
| 1 | Master coarse pitch | −24…24 (0) | semitons |
| 2 | Master fine pitch | −50…50 (0) | centésimos |
| 3, 4 | Modulation X / Y | 0…512 (256) | controles de macro (mapeados nos articuladores) |
| 5 | Timbre pre-decay | 0…1000 (100) | `v/1000` |
| 6 | **Timbre decay time** | 0…10000 (4000) | **10000 = infinito**; senão **t = (21^(v/10000) − 1)·997,5 + 50 ms** (4000 ⇒ 2,4 s; 0 ⇒ 50 ms; 9000 ⇒ 15 s) |
| 7 | **Timbre release time** | 0…10000 (2000) | mesma fórmula (2000 ⇒ 0,89 s) |
| 8 | Timbre fullness | −1000…1000 (0) | `v/1000` (negativo = mais escuro **[HIP]**) |
| 9 | Timbre morphing / randomness | 0…1000 (300) | `v/1000` |
| 10, 11 | Timbre seed 1 / 2 | 0…9999 | 0 = "None"/"Random"; o valor passa por um *hash* inteiro e vira a semente do gerador de harmônicos |
| 12 | **Unison order** | 1…9 (1) | nº de vozes |
| 13 | Unison panning | 0…128 (128) | |
| 14 | Unison volume | 16…128 (128) | |
| 15 | Unison pitch | 0…256 (128) | espalhamento **[HIP: como Harmless 67]** |
| 16 | Unison phase | 0…128 (64) | |
| 17 | **Filter frequency** | 0…256 (128) | `u = v/256` (§2.3) |
| 18 | **Filter resonance** | 0…256 (128) | `r = v/256` |
| 19…25 | Chorus order, depth, speed, delay, spread, cross, mix | | |
| 26…30 | Delay feedback (0…127), time (ticks/192 por tempo, def. 144), stereo offset, volume, modo (0 normal, 1 invertido, 2 ping-pong) | | |
| 31…39 | Reverb lowcut, highcut, predelay, room, diffusion, decay, high damping, color, wet | | |
| 40…42 | EQ níveis bandas 1–3 | ±1800 | centésimos de dB **[HIP]** |
| 43…48 | EQ freq. 1–3, largura 1–3 | 0…65536 | |
| 49 | **Filter type** (`FTypeSelect`, campo `+0x1F0`) | 0…3 | 0 low pass, 1 band pass, 2 band stop, 3 high pass |
| 50 | **Filter mode/order** (`FOrderSelect`, `+0x1F4`) | 0…3 | 0 **off** (default), 1 single, 2 dual, 3 triple (cascata de 2 polos) |
| 51 | FLAT (`FFlatBtn`, `+0x1F8`) | bool | ressonância acompanha o corte |
| 52 | Richness + 5 (`RichnessWheel` 1…11, `+0x1FC`) | 6…16 | **[HIP: log2 do nº de parciais]** |
| 53…62 | bytes empacotados: reverb on/tempo, mod smooth, declick, alt decay, etc. | | |
| 63 | Magic dust (`SGSlider`) | 0…1000 (430) | |
| ≥ 49 (automação) | parâmetros dos articuladores (8 por articulador) | | `FUN_008698e0` |

Mapeamento campo→controle confirmado por `FUN_008649e0` (atualiza a UI a partir do estado) + tabela de
campos publicados do formulário (`tmp/r12/fields.py`) **[DECOMP+STR]**. Ordem dos articuladores (combo
`ArtSelect` sem separadores): 0 Volume, 1 Filter frequency, 2 Filter resonance, 3 Pitch, 4 Unison pitch,
5 Unison phase, 6 Timbre pre-decay, 7 decay time, 8 release time, 9 fullness, 10 morphing, 11 rotation,
12 EQ shifting; depois Forced harmonics, Impulse EQ, Decay EQ, Release EQ **[STR; ordem no stream HIP]**.

### 2.3 Filtro (`FUN_00867cc0`) **[DECOMP; escala final HIP]**

Biquads em cascata (ordem = P50), com `u = P17/256 + articulador`, `r = P18/256 + articulador`:
- **LP:** `c = (1501^u − 1)·0,000665`; frequência angular `ω = (c + 0,003)·2π·s`; `Q = 0,1` (r ≤ 0),
  `50` (r ≥ 1), senão `(2001^r − 1)·0,02495 + 0,1`. Com `s = 0,5` (**[HIP]**: campo `+0xF3C`),
  **`fc ≈ (fs/2)·(c + 0,003)`** ⇒ u = 0,5 → ~620 Hz, u = 0,75 → ~5 kHz, u = 1 → Nyquist.
- **BP/BS:** `c = min(1,0001; (1501^u − 1)·0,0004)`, `Q = (101^r − 1)·0,1199 + 0,01` (0,01…12).
- **HP:** como LP com `c ≤ 0,6`.
- Default: modo **off** (o filtro só existe se P50 > 0). Nos 23 canais: ordem 0 em 9, 1 em 3, 2 em 3, 3 em 8
  **[DADOS]**.

### 2.4 Dados (23 Ogun + 9 Autogun em `.flp`) **[DADOS]**

Script `tmp/r12/ogun_dec.py`. Versões 17–19. Exemplos: "Electric Loop — Kick": decay 100 ms, release 50 ms,
LP triplo u = 0,24; "Swebass": +12 st, uníssono 5, LP triplo; "Turbular Bells 2": decay infinito,
release 20 s, −6 st; os "Pad" do Autogun: −24/−12 st, decay ~12 s, uníssono 4–5, LP u ≈ 0,47.

---

## 3. Fruity DrumSynth Live **[DADOS+STR]**

`0xD5` (v3/v4, 19 467 B): u32 versão, **u32 índice do som selecionado**, depois **120 nomes Pascal**
(u8 tamanho + ASCII) = a biblioteca embutida (`808 Bongo H`, …, `909 Hat C/O/P`, `909 Snare`, …) e, a partir
de 0x4C7, os blocos de parâmetros dos sons (não decodificados). O índice aponta para essa lista:
"Openhat" → `909 Hat O`, "Clap" → `808 Clap`, "Ride" → `909 Ride`, "Snare" → `909 Snare` (8/8 coerentes).
Versão 1 (4232 B, `Default.dsl`): registros de 24 B com nome + dados binários, índice 0.

LYNX: tocar uma amostra de bateria escolhida pelo **nome** (prefixo 808/909 + categoria Kick/Snare/Hat
C/O/P/Clap/Ride/Crash/Tom/Cowbell/Clave/Rim/Maracas/Bongo) do kit padrão do LYNX; se não houver, aproximar
com o mesmo esquema do Fruit Kick (rodada 10 §4.3) para kicks/toms e ruído filtrado para pratos.

## 4. Sakura **[STR+DADOS]**

`0xD5` = `"SSLF"` + u32 0x24C + tag `"1rkS"` (`Skr1`) + **146 × f32**. Nos 510 exemplos o float `k` bate com o
**Tag k** do DFM, normalizado 0…1 (switches com poucos valores em k = 7, 12, 23; "Tune" k = 1 com mediana
0,5; "Delay time sync" k = 112 é inteiro 0…8). Tags úteis: 1 Tune, 2–5 **Amp attack/decay/sustain/release**,
6/11 String 1/2 decay, 17 String mix, 24–25 Lo-Cut cutoff/res, 26–30 Hi-Cut attack/decay/sustain/cutoff/res,
31 exciter noise, 34–37 EG ADSR, 38 LFO rate, 63–77 delay/chorus/reverb. Escalas reais não lidas
(**[HIP]** ADSR com curva tipo Harmless). LYNX: osc1 `saw` + osc2 `triangle`, LP com cutoff ≈ Hi-Cut, ADSR
de amplitude dos Tags 2–5, decay curto (corda pinçada) se String decay < 0,5.

## 5. Morphine **[DADOS]**

`"SSLF"` + chunks com FourCC invertido: `Mor0` (raiz) ⊃ `MPR0` (programa) ⊃ `MVC0`, `MZH0`, `MSP0`, `MBP0`…
(aditivo com até 4 geradores e *morph*; 47–342 KB). Não decodificado. LYNX: preset genérico (saw + LP
médio + ADSR 0,01/0,3/0,7/0,3) e aviso no log.

---

## Recomendações para o LYNX

1. **Despacho pelo `0xC9`:** "GMS", "Ogun", "Autogun" (mesmo parser do Ogun), "Fruity DrumSynth Live",
   "Sakura", "Morphine". Continuar aplicando os eventos de canal (`0xDA`/`0xDB`) como nas rodadas 3/4.
2. **Parser GMS:** validar `"SSLF"`; ler tag (`Syn4`/`Syn3`); 3 nomes de onda (nome base); pular 4 slots +
   (só `Syn4`) 12 B; 212 floats; sanear índices ≥ 197. Fazer a conversão em `float32` com truncamento.
3. **Mapeamento GMS → sintetizador do LYNX** (3 osciladores, AM pelo osc3, ADSR, SVF, envs de filtro e
   pitch, 4 LFOs, mono, porta):

   | LYNX | Origem GMS |
   |---|---|
   | osc k `wave` | tabela §1.4 pelo nome do `.wav` (com o +12 st de Digital/Double Spike/Liquid) |
   | osc k `octave`/`semitones`/`detune` | `oct` global + `st_k` (§1.3) + Δcanal: some tudo em semitons `S`, use `octave = clamp(round(S/12), −3, 3)` e `semitones = S − 12·octave` (se sair de ±11, saturar e registrar); `detune = cents_k` |
   | osc k `level` | `g_osc1`, `g_osc2`, `g_osc3` (§1.3 Mistura). Ruído: se `g_ruído > 0,05` e algum osc tiver nível < 0,05, use esse slot como `noise` com nível `g_ruído` |
   | `pan` dos osc | 0 (o pan do canal vai para o pan do LYNX: `2·x1 − 1`) |
   | AM (osc3 modula) | Mod Kind = AM com m > 0: mover o **osc2 do GMS para o slot osc3 do LYNX** (modulador) com profundidade ≈ `min(1, 3·m)`; o osc3 do GMS ocupa o slot livre se o nível for relevante |
   | Sync / FM | sem equivalente: Sync com m > 0 ⇒ subir o osc1 `12·log2(1 + 3m)` st e trocar `wave` por `saw`; FM ⇒ ignorar e registrar no log |
   | Uníssono | se U ≥ 2 e houver osciladores livres/iguais: duplicar a forma do osc1 nos slots livres com detune `±600·(0,4·x42)²` ct e pan `±x43`; nível × `1/(1 + 0,15(U−1))` |
   | ADSR amp | attack `1,265·x4³` s; decay = `2,3·T_D/8,82` (−40 dB na saída) com `T_D = 50·x5⁴`; **sustain = x6²**; release `2,3·T_R/8,82`. Se o ADSR do LYNX for exponencial por τ, use `τ_LYNX = T/(2·8,82)` (saída é `e²`) |
   | SVF | tipo 0 → `lowpass`, 1 → `bandpass`, 2 → `highpass`; cutoff Hz = `−ln(1 − 0,0005·2^(9,7734·u))·4·fs/(2π)` com `u = x12` (+ keytrack `x14·(nota−57)/128`, avaliado na nota média do canal); ressonância LYNX = `min(0,95; x13)` |
   | Env de filtro | envelope (1 ou 2) com destino 0: attack `7,59·x⁵` s, hold 0, decay `4,6·T_D/8,82` com `T_D = 70(0,8x + 0,2)⁵`, sustain 0, release = release do amp; amount em oitavas = `9,77·A` (A = x16², com sinal do Invert) |
   | Env de pitch | envelope com destino 2: mesmos tempos; amount em semitons = `24·A` |
   | Env destino 3 | *fade* do LFO de mesmo número: aproximar com `fadeIn = attack` do envelope no LFO correspondente |
   | LFOs | LFO 1/2 do GMS → LFO do LYNX conforme destino: 0 → **cutoff** (amount `9,77·x28²` oit), 2 → **pitch** (`12·x28²` st), 3 → **volume** (`x28²`), 1 (modulation) → ignorar salvo se AM ativo. Forma 0 → `sine`, 1 → `tri`, 2 → `tri` com aviso (serra), 3 → `pulse`. Taxa: livre ⇒ período `1/(25(1−x)² + 0,01)` s; sync ⇒ `v` (ou `4v`) **beats**. Retrig ⇒ LFO reinicia por nota. O LFO de pan do LYNX fica livre |
   | Mono / porta | mono = x40 ≠ 0; porta ligado se x11 ≠ 0; tempo em beats = `0,52·(2·x10³)·BPM/60` |
   | Volume do canal | `10^((80x0 − 60)/20) · 10^((50x8 − 25)/20)` |
   | Velocidade → cutoff | se destino 199 = 1: somar `(2·x198 − 1)·0,8` ao `u` (velocidade típica 0,8) **[HIP]** ou, se o LYNX tiver *velocity→cutoff*, mapear direto |
   | FX (Glue ≠ 0) | Delay/Reverb para FX de canal se existirem; demais: log |

4. **Ogun/Autogun (aproximação grosseira):** osc1 `saw` (ou `sine` se fullness ≤ −800 e richness ≤ 3;
   `triangle` se fullness < −300), osc2 igual com +12 st e nível 0,3 para "metais" (Tibetan/Bell/Gong no nome)
   **[HIP]**; transposição `P1 + P2/100` st; ADSR: attack 0, **decay = t(P6)** com sustain 0 (ou sustain 1
   se P6 = 10000), release = t(P7) (§2.2); uníssono P12 → detune ±10 ct nos osciladores livres; filtro só
   se P50 > 0: tipo P49 (LP/BP/notch/HP), cutoff Hz §2.3, ressonância LYNX `= min(0,95; P18/256)`. Volume
   `P0/256`. Registrar no log que o timbre aditivo e os articuladores foram ignorados.
5. **DrumSynth Live:** usar o nome do som selecionado (§3) para escolher a amostra do kit.
6. **Sakura:** ADSR dos Tags 2–5 como 0…1 → tempos com a curva do Harmless (rodada 10 §1.2) **[HIP]**.
7. **Validação sugerida:** renderizar no FL "Basic Acid Bass", "Bass Impacter", "Bell Pure Crystal" (GMS) e
   "Electric Loop — Kick" (Ogun) e comparar envelope, corte e pitch com o LYNX; medir o filtro do GMS em
   48 kHz para confirmar se a tabela de corte ignora `fs`.

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| GMS `FUN_1800c82f0` | `SaveRestoreState`: envelope `"SSLF"` + tamanho + preset | DECOMP |
| GMS `FUN_1800f43d0` / `FUN_1800f4e60` | Escreve / lê o preset (`Syn4`/`Syn3`, 7×512 caminhos, 3 ints, 212 floats, nome) | DECOMP |
| GMS `FUN_1800f1570` | Carrega o `.wav`, renomeia "Devils Horn", gera 512 tabelas | DECOMP |
| GMS `FUN_1800f28f0` | Nomes dos parâmetros 0–60 e dos FX (117–186) | DECOMP |
| GMS `FUN_1800f1bd0`, `FUN_1800f1ee0`, `FUN_1800f2f70` | Defaults, texto dos valores (dB, %, smt, frações de tempo), índice host→interno | DECOMP |
| GMS `FUN_1800f2710`, `FUN_1800f27b0`, `FUN_1800f2850` | Nomes dos 10 FX e de seus Param X/Y | DECOMP |
| GMS `FUN_1800f50c0`, `FUN_1800ef810` | Leitura de parâmetro com matriz MIDI; tabela de destinos | DECOMP |
| GMS `FUN_1800f5250`, `FUN_1800f53e0`, `FUN_1800f58e0` | Note-on/off: afinação, oitava, ADSR, envs AD, porta, fases | DECOMP |
| GMS `FUN_1800f68f0` (+ disassembly `0x1800f6de6…0x1800f72d9`) | Por voz: mistura, uníssono, pitch, razões dos osciladores, escalas ±24/±12 | DECOMP |
| GMS `FUN_1800f7530` | Render: envelopes, destinos, sync/FM/AM, ruído | DECOMP |
| GMS `FUN_1800f5f00`, `FUN_1800efb10`, `FUN_1800efa80`, `FUN_1800ef9f0` | LFOs: taxa livre/sync, formas | DECOMP |
| GMS `FUN_1800f32f0`, `FUN_1800f0b20`, `FUN_1800efbc0` | Filtro ladder 4× sobreamostrado; tabela de corte/realimentação | DECOMP |
| `Synth Waves/*.wav` + `tmp/r12/waves.py` | Espectro das 29 formas | DADOS |
| 321 presets GMS (`tmp/r12/gms_dec.py`, `gms_all.txt`) | Versões, faixas, coerência musical | DADOS |
| Ogun VMT `0x8621f8` (`tmp/r10/findself.py`) | `SaveRestoreState` `FUN_00864d00`, `ProcessParam` `FUN_008684e0` | DECOMP |
| Ogun `FUN_008698e0`, `FUN_00868c70` | Índice de parâmetro / texto (fórmula ms dos tempos, "Infinite") | DECOMP |
| Ogun `FUN_008649e0` + `tmp/r12/fields.py` | Campos `+0x1F0…+0x1FC` = tipo/ordem/flat/richness do filtro | DECOMP+STR |
| Ogun `FUN_00867cc0` | Coeficientes do filtro por tipo | DECOMP |
| DFM `TSynthEditorForm` do Ogun/Autogun/Sakura (`tmp/r12/ogun.dfm.txt`) | Tags, faixas, defaults | STR |
| 258 Ogun, 13 Autogun, 15 DrumSynth, 510 Sakura, 11 Morphine | Contêineres, versões, distribuições | DADOS |

### Questões em aberto

- GMS: escala exata do FM (unidade de fase) e do *Phase Offset Osc 2*/*Unison Phase*; se a tabela de corte
  é mesmo independente de `fs`; semântica "tempos" vs "compassos" do LFO sincronizado (×1 e ×4); fontes
  MIDI 2 e 3 da matriz (mod wheel/aftertouch?); significado dos índices 61–116 e 187–197.
- Ogun: formato dos articuladores (pontos de envelope/LFO/mapas — provavelmente o mesmo objeto do Sytrus,
  ver `REA_rodada11.md`), da tabela de timbre e do *richness*; escala do Master level; valor do campo
  `+0xF3C` (escala do corte); bytes empacotados 53–62.
- DrumSynth Live: formato dos blocos de parâmetros dos 120 sons (seria possível sintetizar sem amostras).
- Sakura: escalas reais dos 146 floats; Morphine: chunks `Mor0`.
