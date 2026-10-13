# Análise comportamental do FL Studio 2026: rodada 17 (REA) — verificação das escalas que o LYNX ainda supõe (Frequency Splitter, Stereo Shaper, Balance/PanOMatic, X-Y, Peak Controller, Kepler, BassDrum, PoiZone, Toxic Biohazard, Sakura, DX10, Sytrus/Harmor, Low Lifter, Ogun e Love Philter)

> **Natureza deste documento.** Continuação de `REA_rodada16.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são fórmulas e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> (ex.: "este campo é a taxa de amostragem porque é escrito pelo setter da taxa") estão explicitadas no texto.

- **Alvos novos** (cópias em `tmp/r17/bin/`):

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave |
  |---|---|---|---|
  | Frequency Splitter | `e2505dc6…90e0e27c` + `engine_x64.dll` `567fcd74…f6ae9593` | Delphi + motor VST2 (`MyPlugin`/`AudioEffectX`) | wrapper: render `FUN_007423b0` (VMT+7), estado `FUN_0073fac0`; motor: `processReplacing` `FUN_1801e56d0`, saída `FUN_1801e5c90`, crossover `FUN_1801e7f10`/projeto `FUN_1801e8190`, texto `FUN_1801e6670`, inclinação `FUN_1801e6440`, `setParameter` `FUN_1801e6d00` |
  | Fruity Peak Controller | `e73ec8da…63ec2d27` | Delphi (`TTestPlug`, VMT `0x6fc540`) | render `FUN_006fda30`, *tick* `FUN_006fdbc0` (VMT+14), nomes de saída `FUN_006fd830` (VMT+4), `ProcessParam` `FUN_006fd2e0`, quantidade/tensão `FUN_006fcef0`, decaimento `FUN_006fd0e0`, LFO `FUN_006fd180`, despacho `FUN_006fc6d0` |
  | Fruity Stereo Shaper | `25766939…a69b5204` | Delphi (VMT `0x6cde90`) | render `FUN_006cf460`, estado `FUN_006ce390`, `ProcessParam` `FUN_006ceb50` |
  | Fruity Balance | `a81c8107…76a4e9d1` | Delphi (VMT `0x6bdcc0`) | `ProcessParam` `FUN_006beaa0`, construtor `FUN_006be800`; DFM `TSynthEditorForm` |
  | Fruity PanOMatic | `029d8cda…296ce26e` | Delphi (VMT `0x6c5a28`) | `ProcessParam` `FUN_006c6650`, inicialização de constantes `FUN_005e1bc0`; DFM |
  | Fruity DX10 | `3a9d58d7…24fde8d9` | Delphi (VMT `0x7343f0`) | conversões `FUN_00735760`, `TriggerVoice` `FUN_00735e30`, `Voice_Render` `FUN_007364a0` |
  | Sakura | `6087f71c…d2179697` + `engine_x64.dll` `e632487c…ae0f1bab` | Delphi + motor C++ | só exportação `CreatePlugInstance` examinada |
  | Fruity Love Philter | `b9f11d63…ba2ff547` (mesma DLL da rodada 16, projeto próprio `/tmp/r17gh_LP`) | Delphi | coeficientes `FUN_00859100`, estágios `FUN_0085d5a0`, taxa `FUN_00854e20`/`FUN_0062b030`, SVF `FUN_006257c0`, mapas X/Y no render `FUN_0085aa10` |
  | `Shared/ildsp_x64.dll` (DSP do Low Lifter) | `b96354e1…faaf1760` | C++ (gerado pelo Faust; classe `ILRDLowFreqEnhancer`, arquivo-fonte declarado `lowFreqEnhancementChebyshev.dsp`) | UI `FUN_180177a80`, `compute` `FUN_180177f50` |

- **Alvos reaproveitados** (projetos Ghidra das rodadas anteriores, abertos só para leitura): motor do Kepler
  (`/tmp/r13gh_kepeng_x64.dll`), motores de BassDrum/PoiZone, Toxic, X-Y, Low Lifter (`/tmp/r15gh_*`), Ogun
  (`/tmp/r12gh_Ogun_x64.dll`), Sytrus/Harmor (`/tmp/r11gh_copy*`).
- **Ferramentas:** Ghidra 12.1.4 *headless*; scripts em `tmp/r17/gh/scripts/` (os da rodada 15/16 + `RefDis.java`:
  desmonta em volta de cada referência a um endereço) e `tmp/r17/casepick.py` (associa índice→`case` em
  `switch` indiretos por tabela de bytes); leitor de constantes PE `tmp/r17/tools_pe.py`; DFM com
  `tmp/r15/dfmc.py`. Manual oficial lido via web (páginas citadas em cada item).
- **Data:** 2026-10-13.

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/`.fst` |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abreviada como `…/plugins/` abaixo).

---

## 1. Frequency Splitter

**O que o LYNX supõe** (`FlEffects.cpp` `frequencySplitter`/`frequencySplitterBand`): barramento 1 = grave,
2 = médio (ou agudo no modo de 2 bandas), 3 = agudo; inclinação sempre 24 dB/oit Linkwitz-Riley; níveis em
centésimos de dB.

### 1.1 Saídas e barramentos **[DECOMP + DOC]**

- O motor (`processReplacing` `FUN_1801e56d0` → `FUN_1801e5c90`) produz **4 pares estéreo**, nesta ordem:
  **0 = grave × nível de envio grave, 1 = médio × nível de envio médio, 2 = agudo × nível de envio agudo,
  3 = soma "Summed Out" = grave·nível(L) + médio·nível(M) + agudo·nível(H)** (parâmetros 11…13 do motor,
  "Low/Mid/High Level (Summed Out)").
- **Modo 2 bandas** (flag do motor `+0x115`; parâmetro "Number of Bands" < 0,5): só um crossover (a frequência
  "Low/High Freq"); o par **médio é escrito como zero** e o agudo continua no **par 2**. Ou seja: em 2 bandas o
  agudo **não** passa para o lugar do médio.
- **Modo 3 bandas:** crossover 1 em f1 → (grave, resto); o resto passa pelo crossover 2 em f2 → (médio, agudo);
  o grave passa por um terceiro crossover em f2 e as duas metades são somadas de volta (compensação de fase do
  grave para casar com os outros ramos).
- O *wrapper* Delphi (`FUN_007423b0`) manda o par 3 (soma) para a **saída principal** do plugin e cada banda i
  (0 grave, 1 médio, 2 agudo) para o **destino de envio próprio da banda**, guardado nos campos `P+0x214`,
  `+0x218`, `+0x21C`. Esse destino é por banda (cada banda tem o seu), e só é usado quando o valor é > 0.
  O envio é feito pela função de *buffer* de envio do host (vtable do host `+0x128`, a mesma usada pelo
  Stereo Shaper). Para o envio das bandas, o manual diz: "Sends are Pre-Fader to the destination Mixer Track" e
  "The 'MAIN OUT' and 'SENDS' carry the same signals" **[DOC `…/plugins/Frequency%20Splitter.htm`]**.
- **Onde ficam os destinos no estado `0xD5`** **[DECOMP `FUN_0073fac0`]**: `u32 versão (4)` + **0x44 bytes
  = 17 × i32 copiados para `P+0x1DC`**. Logo a palavra *w* (contando a versão como 0) é `P+0x1DC+4(w−1)`, e
  **as palavras 15, 16 e 17 são os destinos de envio do grave, do médio e do agudo**. Depois vêm `i32` (valor
  de um controle da UI), `i32` (descartado na leitura), dois `i32` de bits de UI, um byte de opções (bits 0…5:
  UI, `P+0x254`, `P+0x255`, `P+0x28C`, `P+0x28D`, `P+0x2A0`) e, em v ≥ 3/4, mais 6 `i32` de UI.
- **Consequência para o LYNX:** num grafo do Patcher, o barramento de saída do splitter é o **número de
  destino gravado para a banda**, não uma posição fixa. O barramento k carrega a banda i cujo destino
  (palavra 15+i) vale k; o barramento 0 carrega a soma. Se os três destinos forem 1, 2 e 3 (o que os grafos dos
  demos usam), então 1 = grave, 2 = médio e 3 = agudo. **Em 2 bandas, o barramento do médio fica mudo.** A
  correspondência "valor do destino = número do barramento no Patcher" é feita pelo host (FL.exe), que não
  foi decompilado: é coerente com os dados da rodada 14, mas **não confirmada** por código.

### 1.2 Inclinações (combo 0…7) **[DECOMP `FUN_1801e6440`, `FUN_1801e8190` + DOC]**

O texto do parâmetro é "%i dB" com a tabela `{6, 12, 18, 24, 36, 48, 72, 96}`, indexada por
`⌊v_normalizado / 0,125⌋`. Manual: "Choose between -6 and -96 dB/Octave slopes" **[DOC]**. Projeto dos
filtros por valor:

| combo | dB/oit | Estrutura **[DECOMP]** |
|---|---|---|
| 0 | 6 | 1 seção de 1ª ordem (LP/HP complementares) |
| 1 | 12 | 1 biquad com Q = 0,5 e **polaridade do HP invertida** (= Linkwitz-Riley de 2ª ordem) |
| 2 | 18 | estrutura própria baseada em passa-tudo (caminho alternativo `+0x515` do crossover) |
| 3 | 24 | 2 biquads Q = 0,7070 em cascata (= **Linkwitz-Riley 4**: Butterworth 2 ao quadrado) |
| 4 | 36 | 3 biquads Q = 0,5412 / 1,3066 / 0,7070, HP invertido |
| 5 | 48 | 4 biquads Q = 0,5412 / 1,3066 / 0,5412 / 1,3066 (= **LR8**: Butterworth 4 ao quadrado) |
| 6 | 72 | 6 biquads (Q de Butterworth 6: 0,5176 / 0,7071 / 1,9319, duas vezes) |
| 7 | 96 | 8 biquads (Q de Butterworth 8: 0,5098 / 0,6013 / 0,9000 / 2,5629, duas vezes) |

Ou seja: **os modos pares são Linkwitz-Riley** (Butterworth ao quadrado). O LYNX acerta para o combo 3 e
erra para os outros. Há também o modo "Linear phase / FFT" (parâmetro "Filter Mode": Normal/FFT); o
manual: "Normal … Near zero-latency Infinite Impulse Response (IIR) filters"; linear phase com latência de
"between 12 and 290 ms" **[DOC]**. O modo FFT não foi detalhado.

### 1.3 Frequências e níveis **[DECOMP `FUN_1801e6670`, `FUN_1801e6d00` + DOC]**

- Frequência: **`f = 30 · 2^(9,0589·x)` Hz** (x = valor normalizado 0…1; 30 Hz…16 kHz). Equivale à fórmula
  `30·(16000/30)^x` usada pelo LYNX. Manual: "between 30 Hz and 16 kHz" **[DOC]**.
- Nível (envio e soma), x normalizado: **ganho linear = 2x para x < 0,5; = 1 + 13,8866·(x − 0,5) para x ≥ 0,5**
  (0 → mudo, 0,5 → 0 dB, 1 → +18,0 dB). Manual: "+18 dB to -∞ (muted)" **[DOC]**. A palavra do `.flp` em
  centésimos de dB (rodada 14) é a forma que o *wrapper* grava; a conversão dB ↔ x não foi relida aqui.
- Mudo/solo por banda zeram/isolam os ganhos (`FUN_1801e6d00`, campos `+0x1144…+0x114F`).

---

## 2. Fruity Stereo Shaper dentro do Patcher

**O que o LYNX supõe:** só a matriz L/R (palavras 1…4). Pergunta: o que sai no barramento 1.

**[DECOMP `FUN_006cf460`, `FUN_006ce390` + DOC]**

- Estado `0xD5` = `i32 versão` + **8 × i32** em `P+0x120…0x13C` = parâmetros 0…7: 0…3 matriz (R→L, L, R,
  L→R; ×12800 = 1,0 como já no LYNX), 4 *delay*, 5 fase, **6 = destino do sinal "In/Out Difference"**
  (`P+0x138`), **7 = ordem (pré/pós) dos efeitos** (`P+0x13C`).
- No render: se o destino (parâmetro 6) for > 0, a entrada é copiada antes do processamento; depois do
  processamento calcula-se **entrada − saída** e esse sinal é **somado ao *buffer* de envio** do destino
  escolhido (mesma função do host `+0x128` do Frequency Splitter). A **saída principal (barramento 0)** é o
  sinal processado (matriz + efeitos).
- Manual: "sends a 'difference' signal to the selected sidechain Mixer track … (Input - Output = Difference)";
  preset "aMid - Side splitter": "An 'adapted' Mid signal remains on the host Mixer track and a Side signal is
  sent to the IN/OUT Difference track"; "Mid = (L+R)/2", "Side = (L-R)/2" **[DOC `…/plugins/Fruity%20Stereo%20Shaper.htm`]**.
- **Para o LYNX:** barramento 0 = saída da matriz; **barramento k (k = valor do parâmetro 6) = entrada − saída**.
  No preset M/S, a matriz deixa só o *mid* no barramento 0 e a diferença (o *side*) sai no barramento do
  destino. Mesma ressalva do §1.1: a igualdade "destino k = barramento k do Patcher" é feita pelo host e não
  foi lida no FL.exe (**não confirmada** por código; coerente com os dados da rodada 14).

---

## 3. Fruity Balance e Fruity PanOMatic — volume

**O que o LYNX supõe:** 0…320, 256 = 0 dB, curva `(11^x − 1)/10`, x = v/256.

**Confirmado [DECOMP + DFM]:**

- DFM `TSynthEditorForm`: Balance `VolWheel` **Max 320, Value/Default 256**, `PanWheel` −128…128 (0);
  PanOMatic `VolWheel` **Max 320, Default 256**, `PanWheel` 0…256 (128), LFO amount 0…128 (64), LFO speed
  0…55000 (27500), forma Sine/Triangle/Square, alvo OFF/PAN/VOL.
- PanOMatic (`FUN_006c6650`, parâmetro 1): **ganho = (11^(v/256) − 1)/10** (o `ln 11` é calculado na
  inicialização `FUN_005e1bc0`); v = 256 → 1,0 exatamente.
- Balance (`FUN_006beaa0`, parâmetro 1): x = v/320; **ganho = (11^(1,25·x) − 1)·(1/(10·cos(π/4)))** × lei de
  balanço. Como 1,25·v/320 = v/256, é a mesma curva; o fator `1/cos(π/4)` cancela o `cos/sin(π/4)` da lei de
  pan no centro, então **v = 256 → 0 dB**, v = 320 → **+5,59 dB**, v = 128 → −12,7 dB.
- Lei do Balance (mesma função): ângulo `(b/128 + 1)·π/4` (b = −128…128) dá os ganhos L/R por seno/cosseno,
  multiplicados por `1 − 0,5·|sen(π·|b/256|)|`; quando b ≠ 0 há ainda um termo cruzado (o canal do lado
  oposto recebe a diferença dos dois ganhos) — é o "true panning … by mixing left and right channels" do
  manual **[DOC `…/plugins/Fruity%20Balance.htm`]**.

**Conclusão: a suposição do LYNX está correta** para o volume dos dois plugins.

---

## 4. Fruity X-Y Controller — curva de tensão

**O que o LYNX supõe:** `level^(2^(−2·tensão))`.

**Confirmado [DECOMP `FUN_006c6360` (tensão), `FUN_006c6d90` (*tick*, VMT+14)]:**

- Tensão t = −128…128 → sinal s = sign(t) e **k = (1001^(|t|/128) − 1)·0,1** (t = 128 → k = 100; 64 → 3,06;
  32 → 0,46).
- A posição suavizada p (0…1) de cada eixo vira:
  - s = 0: `y = p` (reta);
  - **s > 0: `y = 1 − ((k + 1)^(1 − p) − 1)/k`** (côncava: sobe rápido no início);
  - **s < 0: `y = ((k + 1)^p − 1)/k`** (convexa: sobe devagar).
- Saída = `mín + y·(máx − mín)` (inteiros 0…65536, recortados no intervalo), publicada como controle
  **eixo 0 = saída 0 (X), eixo 1 = saída 1 (Y)** (o índice passado ao host é o do eixo).
- Manual: "Tension (TNS) … alter the shape of the curve used to map the coordinate values to the controller
  values" **[DOC `…/plugins/Fruity%20X-Y%20Controller.htm`]**.

**A fórmula do LYNX está errada** (é uma potência; a do FL é uma exponencial normalizada). Mesma família da
tensão do Peak Controller (§5).

---

## 5. Fruity Peak Controller

**O que o LYNX supõe:** saídas 0/1/2 = peak/LFO/produto; queda até −60 dB = 20/speed s; ataque não modelado.

### 5.1 Índices das saídas **[DECOMP `FUN_006fd830` + `FUN_006fdbc0` + DOC]**

O nome de saída (despacho 7 = "nome do controle de saída") vem da tabela `{"Peak+LFO", "Peak", "LFO"}`:
**saída 0 = Peak + LFO (soma), 1 = Peak, 2 = LFO**. No *tick* as três são calculadas assim (escala interna
0…2^30 = 0…1, recortada em 0…1): `peak_out`, `lfo_out` e **`peak_out + lfo_out`**. Manual: "Peak Ctrl -
Peak+LFO - The combination (sum) of the above two automation signals" **[DOC `…/plugins/Fruity%20Peak%20Controller.htm`]**.
→ **O LYNX tem a ordem e a natureza da saída 0 erradas** (não é produto).

### 5.2 Fórmula da saída Peak **[DECOMP `FUN_006fcef0`, `FUN_006fdbc0`]**

Com os parâmetros da rodada 14 (Base 0…65536, Amount −256…256, Tension −128…128, Decay 5…128):

- `a = sign(A)·(6^(|A|/256) − 1)·0,4` (A = 256 → +2,0 = 200 %; manual: "values from -200% … to 200%" **[DOC]**).
- Tensão: `k = (1001^(|T|/128) − 1)·0,1`; se T ≠ 0, a quantidade é dividida por k.
- Forma (p = detector de pico, §5.3): T = 0 → `y = p`; **T > 0 → `y = k − ((k + 1)^(1 − p) − 1)`**;
  **T < 0 → `y = (k + 1)^p − 1`**; e então `peak_out = Base/65536 + a·y/k` (= `a·p` com T = 0).
  É exatamente a curva do X-Y (§4) multiplicada por `a`.
- LFO: base 0…65536, `a_LFO = sign·(6^(|v|/256) − 1)·0,2` (máx. ±1,0 = 100 %; manual: "-100% … to 100%"
  **[DOC]**), mesma tensão; tabela de forma indexada pela fase.

### 5.3 Detector, ataque e queda **[DECOMP `FUN_006fda30`, `FUN_006fd0e0`, `FUN_006fc6d0`]**

- **Ataque instantâneo:** a cada bloco de áudio, `p = max(p, pico absoluto de L e R no bloco)` (valor linear,
  sem suavização e sem limite em 1,0). Não existe controle de ataque (o manual não lista nenhum).
- **Queda linear em tempo musical:** a cada *tick* do host (método `NewTick`, VMT+14), `p ← p − d`, recortado
  em 0, com **`d = (10001^(D/128) − 1)·0,0192 / PPQ`** (D = 5…128). O divisor é o campo `+0x188`, gravado pelo
  despacho 14 com o 3º inteiro do registro recebido (assinatura de compasso: passos por compasso, passos por
  tempo, **PPQ**). Logo, por **semínima**, p cai `(10001^(D/128) − 1)·0,0192`:

  | D | queda por semínima | tempo para cair de 1,0 a 0 |
  |---|---|---|
  | 5 | 0,0083 | 120 semínimas |
  | 33 | 0,187 | 5,3 semínimas |
  | 66 (padrão) | 2,20 | 0,45 semínima |
  | 100 | 25,6 | 0,04 semínima |
  | 128 | 192 | 0,005 semínima |

  (Assume-se, como no SDK do FL, que `NewTick` é chamado uma vez por *tick* de PPQ; o código só mostra a
  divisão por PPQ.) **A suposição "20/speed s até −60 dB" do LYNX não corresponde**: a queda é linear (não
  exponencial) e proporcional ao andamento.
- Velocidade do LFO (`FUN_006fd180`): período = `(1001^(v/65536) − 1)·0,128` semínimas (v = 32950 → 4
  semínimas = 1 compasso 4/4; 65536 → 128 semínimas).

---

## 6. Kepler (`FlGenerators.cpp` `synthFromKepler`)

Motor `kepeng_x64.dll`: texto `FUN_18001db30`, *setter* global `FUN_180019a20`, *setter* por voz
`FUN_180022840` (8 vozes), voz `FUN_180023ab0`, índice do painel → interno `FUN_180016300`. Todos os valores
abaixo vêm de tabelas de 11 pontos com interpolação linear em x·10 **[DECOMP]**.

| Parâmetro (k do painel → interno) | LYNX supõe | Confirmado |
|---|---|---|
| **Octave Transpose** (32 → 0x53) | ? | **`round(2x) − 1` oitavas: −1, 0, +1** (voz, `+0x1260`). Manual: "Transpose the sound -1, 0 or +1 octave" **[DOC `…/plugins/Kepler.htm`]** |
| **Env1 Attack** (26 → 0x3D) | `10·x³` s | tempo **T_A = tabela {0,003; 0,004; 0,013; 0,05; 0,128; 0,243; 0,401; 0,7; 1,3; 2,403; 3,049} s**. Implementação: polo único em direção a 1,7 com coeficiente `1 − 0,41176^(1/(T_A·fs))`; como 0,41176 = 0,7/1,7, o envelope **chega a 1,0 em exatamente T_A** e passa ao decay |
| **Env1 Decay** (27 → 0x3E) | `10·x³` s | **T_D = {0,0015; 0,0027; 0,043; 0,141; 0,328; 0,651; 1,144; 2,053; 3,956; 8,466; 10,124} s** (é também o texto exibido); `nível = S + (1 − S)·u`, u multiplicado por `0,1^(1/(T_D·fs))` por amostra → **T_D = tempo para a distância ao sustain cair a 10 % (−20 dB)** |
| Env1 Sustain (28 → 0x3F) | x | **S = x** (linear) |
| **Env1 Release** (29 → 0x40) | `10·x³` s | **T_R = {0,001; 0,002; 0,02; 0,112; 0,321; 0,572; 1,063; 1,964; 3,38; 6,91; 10,967} s**; nível multiplicado por `0,1^(1/(T_R·fs))` → T_R = tempo para cair 20 dB |
| **LFO1 Rate** (5 → 0x30) | `0,1·200^x` Hz | **f = 21,739·2^(−T(x)) Hz**, T = {7,4422; 5,8074; 4,3499; 3,6563; 3,1131; 2,6712; 2,0463; 1,3093; 0,8515; 0,2838; 0}: 0,125 / 0,388 / 1,07 / 1,72 / 2,51 / **3,41** / 5,26 / 8,77 / 12,0 / 17,9 / **21,7 Hz** em x = 0; 0,1; …; 1 (mesma função no texto e no DSP) |
| **Main Volume** (33 → 0x48) | 0,7 = 0 dB | **m(v) = 0,34968·v − 0,32754·v² + 1,39229·v³** (aplicado com sinal invertido, irrelevante para nível) e ganhos L/R = `m(v)·√(1 − pan)` / `m(v)·√pan`. Com pan central: **v = 1 → 1,000 (0 dB)**, v = 0,8 → −5,1 dB, **v = 0,7 → −8,0 dB**, v = 0,5 → −14,5 dB. A suposição "0,7 = 0 dB" está errada |
| **Cutoff** (19 → 0x25) | `20·2^(10x)` Hz | **f₀ = 2^(C(x)) Hz**, C = {2; 2,4594; 3,3219; 4,3219; 5,6837; 6,9542; 8,543; 10,4717; 12,388; 15,1635; 15,6096} → **4 / 5,5 / 10 / 20 / 51 / 124 / 373 / 1 420 / 5 360 / 36 700 / 50 000 Hz** em x = 0…1. Corte final = `f₀ · 2^(soma das modulações em oitavas)`, **recortado em 5…30 000 Hz** |
| **Env1→Cutoff Amount** (22 → 0x2C) | ? | **E(x) oitavas = {0; 0,389; 1,681; 2,888; 4,017; 5,039; 6,069; 7,288; 8,475; 8,957; 8,972}**; a soma em oitavas é `polaridade (±1, k 21) · envelope (0…1) · E(x)` |
| Outras modulações do corte | — | *key follow*: `(nota − 24)/12 · KF` oitavas; LFO1→cutoff somado no mesmo expoente **[DECOMP `FUN_180023ab0`]** |
| HP Freq (18 → 0x24) | — | `20 + 2680·x^1,6` Hz (texto e DSP) |
| Filter Slope (Exo) | — | 6/12/18/24 dB/Oct **[DECOMP]**; manual Exo: "Choose from 6 to 24 dB per octave" **[DOC `…/plugins/Kepler%20Exo.htm`]** |
| **Chorus I / II** (30 → 0x45) | taxas fixas | modo = `round(3x)`: Off / I / II / I+II **[DECOMP]**; manual: "I - Emulates the Juno 6 Chorus I. II - Emulates the Juno 6 Chorus II" **[DOC]**. **Taxas do LFO do chorus: não confirmadas** |

`fs` acima é o campo de taxa da voz (`+0x2DF4`), lido no lugar da taxa de amostragem em todos os
coeficientes; não foi checado se o Kepler sobreamostra.

---

## 7. BassDrum (`synthFromBassDrum`)

Motor: `setParameter` `FUN_18002ae90` (vtable `+0xD8`), início de nota `FUN_180028ff0`, render da voz
`FUN_180029440` **[DECOMP]**.

| Parâmetro | LYNX supõe | Confirmado |
|---|---|---|
| 6 Base (frequência final) | `2^(4x+4)` Hz | texto `2^(4x + 4)` Hz (rodada 15). No DSP, vira o incremento de fase `base·(1024/N)/44100` |
| **7 Peak** | `base·2^(6·peak)` | guardado como **`P = 100·x³`**; frequência instantânea **`f(t) = base · (1 + P'·s(t)⁴)`**, com `P' = P·(1024/N)` e s(t) o envelope de *slide* abaixo. N é um inteiro lido de um objeto compartilhado (`**(voz+0x138)`) que não foi identificado; **se N = 1024 (tamanho de tabela), f(0) = base·(1 + 100·x³)**. Escala final **não confirmada** por causa de N |
| **8 Slide Time** | ? | `T_s = 20·x³ + 0,001`; s começa em 1 e é multiplicado por `(1 − 0,0002/T_s)` a cada amostra → **constante de tempo de s ≈ 5000·T_s amostras**; o desvio de pitch cai com s⁴ |
| **2 Duration** | ? | `d = x³`; envelope mestre **linear de 1 a 0 em (d·44100 + 44,1) amostras** (≈ `x³ + 0,001` s a 44,1 kHz; o número 44100 é constante no código, não a taxa do host). Manual: "There is no release time" **[DOC `…/plugins/BassDrum.htm`]** |
| 15 Click Amount / 13 Cut / 14 Freq | ? | Manual: "LEVEL - Click loudness. CUT - … filter low-frequencies … FREQ - Frequency of click Oscillator" **[DOC]**. Escalas do clique: **não confirmadas** |
| Noise | ? | Manual: não há gerador de ruído; o "ruído" é a camada de amostra ("usually to add noise or texture"), Delay "-100% … +100%", MIX "(0 to 100%)" **[DOC]** |
| 0 Output Level | — | manual: "+/- 20 dB final output level trim" **[DOC]**; texto `40(x − 0,5)` dB (rodada 15) |
| 3 Butt, 4 Drive, 5 Attack | — | guardados como x², x, x (sem escala física confirmada) |

---

## 8. PoiZone

`setParameter` `FUN_18002e3f0` (vtable `+0xD8`), suavização por bloco `FUN_1800312e0`, coeficientes do filtro
por voz `FUN_18002c8a0`, osciladores `FUN_18002cb70`/`FUN_18002ce40`, render `FUN_18002d340` **[DECOMP]**.

- **Corte (18 "Flt. Cutoff")** — LYNX supõe `20·1000^x`. Confirmado: expoente
  `v = 1,1·x + (mods)` recortado em **0…0,91**, frequência **`f = 2^(10·v + 6)` Hz** (normalizada pela taxa
  da voz e convertida por um polinômio de afinação `3,6g − 1,6g² − 1`). Sem modulação: **x = 0 → 64 Hz;
  0,25 → 430 Hz; 0,5 → 2,9 kHz; 0,75 → 19,5 kHz; x ≥ 0,827 → teto 35 kHz** (ou seja, `64·2^(11x)` Hz até o
  teto). Mods somadas em v: *key track* `(nota − 56)·0,0084·KT`, velocidade `(vel − 0,5)·0,5·VT`, EG, LFO, MW.
- **EG Amount (10)** — LYNX supõe ±4 oitavas. Confirmado: a = `2x − 1` (−1…1); com destino Cutoff, soma
  **`sign(a)·a²·env`** em v → **±10·a² oitavas** (máx. ±10 oitavas, curva quadrática). Destinos Frq A/AB:
  `2^(⌊24a⌋·env/12)` (até ±24 semitons, quantizado em semitons) **[DECOMP]**. Manual: "AMOUNT: Amount can be
  -100 to +100%" **[DOC `…/plugins/PoiZone.htm`]**.
- **Osc Balance (8)** — confirmado: nível do OscA = `(1 − bal)·(1 − noise)`, OscB = `bal·(1 − noise)`
  (bal = x, noise = parâmetro 9). **x = 0 → só A, x = 1 → só B.** Manual: "fully left to hear OSC A and fully
  right to hear OSC B, the 12 O'clock position is a 50% mix" **[DOC]**.
- **LFO Amount (26)** — confirmado: `l = 2x − 1`; com destino Cutoff, soma `sign(l)·l²·LFO(t)` em v
  (**±10·l² oitavas**); destinos de pitch como o EG. Manual: "-100% … 0% … 100%" **[DOC]**.

---

## 9. Toxic Biohazard

`setParameter` `FUN_18003dca0` → `FUN_18003dcc0` (despacha para `FUN_18003e620`, `FUN_18003e510`,
`FUN_180040080`, `FUN_18003e730`, `FUN_18003eb20`, … um por operador) **[DECOMP]**. Conversões confirmadas
(x = valor normalizado):

| Parâmetro | LYNX supõe | Confirmado |
|---|---|---|
| **O1…O6 Coarse** (31…36) | `round((x − 0,5)·48)` | **`trunc(198·x − 99)` semitons, recortado em −99…+99**. Manual: "within a range of -99 to +99 semitones" **[DOC `…/plugins/ToxicBio_oscillators.htm`]** |
| **O1…O6 Fine** (37…42) | — | **`trunc(198·x − 99)·0,01` semitom** (±0,99 st = centésimos). Conferido com a tabela de razões do manual (3:1 = "Pitch 019 / Detune 002" = 19,02 st) **[DOC `…/plugins/ToxicBio_matrix.htm`]** |
| Transpose (2) | — | `trunc(48x − 24)` semitons |
| **Ataque/Decay/Release** dos operadores (73…96) e mestres (3, 4, 6) e do filtro (19, 20, 22) | `10·x³` s | **`T = max(50·x⁴, 0,00101)`** (unidade: o mínimo de 1,01 ms indica segundos; a unidade não foi lida no DSP). Release mestre (6): `0,35·T + 0,02`. Sustain = **x²** |
| **Matriz FM** (109…144) | ? | célula = **`(⌊100·x⌋·0,01)²`** (0…1, quadrática em passos de 1 %). Manual: "multiplier controls (0 to 100%)" **[DOC]**. **Conversão para índice de modulação: não confirmada** (o uso no render do operador não foi localizado) |
| Frq Shift (43…48) | — | `(2x − 1)^5 · 4000` Hz |
| Mix (103…108) / Pan (97…102) | — | x² / x |
| **Flt Cutoff** (15) | ? | guardado cru (x) e suavizado no render (`FUN_1800406d0`); **escala em Hz não confirmada** |
| Flt Eg Amnt (23) | — | `sign(2x − 1)·(2x − 1)²`; Flt Lfo1 Amnt (24): `sign·(x^1,5)²` |
| LFO1/2 Speed (13, 14) | — | `20·x³ + 0,01` |
| Glide (10) | — | x³ |

---

## 10. Sakura

**Não confirmado.** O motor (`Sakura/engine_x64.dll`) só exporta `CreatePlugInstance` e não tem textos de
unidade; o *wrapper* tem apenas formatos genéricos (`%.0fHz`, `%.1f%%`). As escalas do ADSR de amplitude
(Tags 2–5), do Hi-Cut e do *string decay* exigem localizar o DSP no motor — não feito nesta rodada.
Manual: sem faixas numéricas; "Hi Cut" aplica "both filtering and enveloping on the Exciter waveform" e o
corte é descrito como "Cutoff frequency for the 'High Pass' filter" (sic); "Decay - String decay time"
**[DOC `…/plugins/Sakura.htm`]**.

---

## 11. Fruity DX10 — profundidade de FM

**O que o LYNX supõe:** índice (ciclos de desvio de fase por unidade do modulador) = `2·p²`.

**Confirmado [DECOMP `FUN_00735760`, `FUN_00735e30`, `FUN_007364a0`]:**

- A fase da portadora é uma variável `φ` em **unidades de meio ciclo** (período 2: é envolvida em ±1 e a
  forma de onda é o polinômio `φ + φ³·(r·(φ² − 1) − 1)` do estilo mda, r = "Waveform"). Por amostra:
  **`φ += D + L₁·M₁ + L₂·k₂·M₂`**, onde D é o incremento da portadora e `Mᵢ` é o seno do modulador i (tabela
  de 16384 pontos do host). **É FM verdadeira** (modulação somada ao incremento), não PM.
- **Incremento da portadora:** `D = 8,1758·2^(oct+1)·2^(nota/12)/fs` → portadora em
  **`f_c = D·fs/2 = 8,1758·2^(nota/12)·2^oct` Hz** (oct = −3…+3 do "COARSE"; 0 = afinação da nota MIDI).
- **Frequência do modulador i:** incremento de fase `(coarse + fine)·2^30·D` em 2^32 → **`f_m = R·f_c/2`**
  (R = razão coarse+fine da rodada 10). Ou seja, com R = 1 o modulador soa uma oitava **abaixo** da
  portadora, conforme o código.
- **Nível do modulador:** `L = 0,0002·p²·(44100/fs) · min(2^(nota/12), 50) · (S·(V − 64) + 64)` com p = "Init"
  (e o mesmo para "Sustain"), S = sensibilidade de velocidade (0…1) e V = velocidade mapeada pela curva de
  velocidade (só com S = 0 o fator é a constante 64). O nível vai de Init para Sustain com o coeficiente do
  "Time".
- **Índice de modulação equivalente** (desvio de pico ÷ frequência do modulador), juntando tudo e com S = 0:
  - desvio de frequência de pico = `L·fs/2` Hz;
  - **β (rad) = (L·fs/2)/f_m = 0,0002·64·44100·p² / (R·2^(oct−1)·8,1758) ≈ `69,0·p² / (R·2^oct)`** para notas
    até ~67,7 (acima disso o fator `min(·, 50)` reduz β);
  - em **ciclos de desvio de fase**: `β/(2π) ≈ 11,0·p²/(R·2^oct)`.
  A suposição `2·p²` do LYNX subestima a profundidade em ~5×, e o FM do DX10 é relativo a `f_m = R·f_c/2`.
- O segundo modulador entra **em paralelo** (somado ao mesmo incremento da portadora), com o seu próprio
  nível `k₂·L₂` **[DECOMP]**.
- "Mod Thru" (p12): mistura `0,25·p²` do modulador na saída (rodada 10; o render soma `thru·M₁` à saída).
- Manual: só descreve os controles; não fala em índice **[DOC `…/plugins/Fruity%20DX10.htm`]**.

---

## 12. Sytrus e Harmor

### 12.1 Sytrus — matriz FM e articulador de corte

- **Matriz FM:** a célula `v` vira `g = sign(v)·(11^(|v|/256) − 1)/10` e cada entrada FM recebe
  **quantidade 4·g** (feedback `g·35/128`) **[DECOMP `FUN_0086dd40`, constantes `0x86e664/668/66c`]**.
  Manual: "if the value is negative … the modulation phase will be inverted" **[DOC `…/plugins/Sytrus.htm`]**.
  **A unidade de fase em que essa quantidade é somada (ciclos ou radianos) não foi confirmada nesta rodada**
  (o consumidor da lista de modulações no render do operador não foi localizado a tempo); portanto
  `4·g` ciclos continua **não confirmado**.
- **Articulador de corte: bipolar.** Manual: "Cutoff (CUT) - Offsets the cutoff levels for the filter (graph
  bottom = -100 offset, graph top = +100 offset)" **[DOC]** → nível 0,5 = sem deslocamento. O código soma a
  saída do articulador a `c = p[2]/256` (rodada 11 §1.7) **[DECOMP]**; a profundidade é `p[1]/128`. Coerente
  com a rodada 16 (no Love Philter, os articuladores de cutoff/res/pan gravam o modo bipolar) **[DADOS]**.

### 12.2 Harmor

- **Volume com o articulador desligado:** **não confirmado.** Manual: "Many envelopes need to be activated
  with the switch in the lower-left corner before articulating the target" e "the voice ends when the volume
  envelope has ended" **[DOC `…/plugins/Harmor.htm`]** — não diz o que acontece com o envelope desligado.
- **Escala de frequência dos filtros:** **não confirmada.** O texto "%d Hz" encontrado (`FUN_0083bd30`) é do
  editor de envelopes ("Hz scale"), relativo à frequência da nota, e não do knob de filtro; a suposição
  `73,4·2^(7,855·v/8192)` (herdada do Harmless) continua sem evidência direta.

---

## 13. Low Lifter — o "realce" de harmônicos

**O que o LYNX supõe:** `+0,25·preBoost` dB num *low shelf*.

**Confirmado [DECOMP `ildsp_x64.dll` `FUN_180177a80`, `FUN_180177f50` + DOC]:** o DSP não está na DLL do
plugin: `Low Lifter_x64.dll` carrega `LowFreqEnhancer_CreateInstance` de `Shared/ildsp_x64.dll`, cuja classe
`ILRDLowFreqEnhancer` foi gerada pelo Faust a partir de `lowFreqEnhancementChebyshev.dsp`.

- Controles (faixas da UI Faust): Dry/Wet 0…1; **Low Gain −120…+18 dB**; **Pre Gain 0…18 dB**; **Slope
  −60…0 dB**; Diffusion Mix 0…1, Depth −0,99…0,99; **High Gain −120…0 dB**; **Crossover 20…120 Hz** e **Low
  Cut 20…120 Hz** (escala log); chaves Multiband, Lookahead, Invert Polarity, Low/High Switch.
- **Gerador de harmônicos (por banda):**
  1. o sinal da banda grave é multiplicado por `g = 10^(PreGain/20)` (e por −1 se *Invert Polarity*);
  2. um seguidor de pico com *lookahead* (atraso de leitura, cadeia de máximos com retenção e quatro estágios
     de liberação) dá o envelope E, **limitado por baixo em 1,0**: `u = x_atrasado / max(1, E)` — é o
     "lookahead limiter [that] will prevent these boosted frequencies from exceeding 0 dB" do manual;
  3. **polinômios de Chebyshev até a 3ª ordem:** com `a = 10^(Slope/20)` e `b = 10^(Slope/10) = a²`,
     **`y = [u + a·T₂(u) + b·T₃(u)] / (1 + 2a)`**, onde `T₂(u) = 2u² − 1` e `T₃(u) = 4u³ − 3u`;
  4. `y` passa por um filtro de 1ª ordem (remove o DC de T₂) e segue para o difusor/mistura.
  Para uma senoide de amplitude A ≤ 1 (após o pré-ganho), T₂ e T₃ produzem a **2ª harmônica com amplitude
  `a·A²`** e a **3ª com `b·A³ = a²·A³`** (relativas, antes da normalização 1/(1+2a)) — ou seja, o "Slope" é a
  queda em dB por ordem de harmônico e o "Pre Gain" controla quanto da banda entra na região não linear.
- O resto da cadeia (crossover, ganhos das bandas, *low cut* de 2ª ordem, difusor passa-tudo, dry/wet) já foi
  descrito na rodada 15 §5. Manual: "dynamically adds harmonics to bass frequencies ... psychoacoustic
  principle"; "Slope - Harmonics decay slope in dB"; "Low cut - Cutoff of a final-stage second-order highpass
  filter" **[DOC `…/plugins/Low%20Lifter.htm`]**.
- **Aproximação correta para o LYNX:** em vez de `+0,25·preBoost` dB, aplicar à banda abaixo do crossover um
  *waveshaper* `y = (u + a(2u² − 1) + a²(4u³ − 3u))/(1 + 2a)` com `u = clamp(g·x, −1, 1)` (o limitador do FL
  é dinâmico; o `clamp` é a parte que o LYNX consegue reproduzir sem *lookahead*), seguido de um passa-altas
  de DC e da mistura com os ganhos de banda.

---

## 14. Ogun — nível mestre e escala do filtro

`FUN_0086a570` (nível mestre), `FUN_00864800` (taxa), bloco de voz com `+0xF3C` (render), texto
`FUN_00868c70` **[DECOMP]**.

- **Master level (P0, 0…256, padrão 128)** — LYNX supõe `P0/128`. Confirmado: **ganho = (3^(P0/128) − 1)/2**
  (o `ln 3` é calculado na inicialização; o fator 0,5 é a constante `0x8b9654`). P0 = 128 → 1,0 (0 dB);
  256 → 4,0 (**+12,0 dB**); 192 → 2,10 (+6,4 dB); 64 → 0,366 (−8,7 dB). O texto mostra `P0/128·100 %`
  (0…200 %), como no manual: "Master level - Ranges from 0 to 200%" **[DOC `…/plugins/Ogun_master.htm`]** —
  o percentual exibido é linear, mas o ganho aplicado não é.
- **Escala `s` do filtro** — rodada 12 supôs s = 0,5. Confirmado: **`s = (44100/fs_int) · (0,5 se fs_int <
  88200)`**, onde `fs_int` = taxa do host × fator de sobreamostragem do plugin (o objeto de taxa recebe
  `taxa·fator` em `FUN_00864800`, e o seu campo `+0x20` é `44100/taxa`, escrito por `FUN_0062b030`).
  Como o filtro roda em `fs_int`, o corte em Hz é:
  - **fs_int < 88,2 kHz: `fc = 22050·(c + 0,003)` Hz** → u = 0 / 0,25 / 0,5 / 0,75 / 1 → 66 / 143 / 620 /
    3 590 / 22 060 Hz (u = P17/256 + articulador; `c = (1501^u − 1)·0,000665`);
  - fs_int ≥ 88,2 kHz: `fc = 44100·(c + 0,003)` Hz.
  A 44,1 kHz sem sobreamostragem, s = 0,5 exatamente (a suposição da rodada 12 bate nesse caso).

---

## 15. Love Philter — itens extras da rodada 16 (pedido do coordenador)

### 15.1 Fator `k` da unidade (`+0xC4`) e a fórmula do SVF **[DECOMP]**

- `k` é copiado em `FUN_0085d5a0` do campo `+0x20` do objeto de taxa do plugin. Esse objeto recebe, em
  `FUN_00854e20`, **taxa do host × fator de sobreamostragem** (`+0x168C`, obtido do sobreamostrador pela
  opção `DQOverSelect`, dobrada com HQ), e o seu método de taxa (`FUN_0062b030`) grava `+8 = taxa` e
  **`+0x20 = 44100/taxa`**. Logo **`k = 44100 / fs_interno`**, como a rodada 16 supôs.
- **SVF "Chocolate" (tipo 0):** o processamento (`FUN_006257c0`, via ponteiro de função `0x8d36d0`) é o SVF de
  Chamberlin: `LP += f·BP; HP = in − LP − q·BP; BP += f·HP` (L, B e H saem dos três *buffers* e são somados com
  os ganhos L/B/H). O coeficiente é **`f = 2·sen(w)`**, `w = min(0,625; (x + 0,005)·k)`, `x = (51^c − 1)·0,0159`
  (seno = rotina `Sin` do Delphi, `FUN_0040bb50`). Como no Chamberlin `f = 2·sen(π·fc/fs)`,
  **`fc = (x + 0,005)·44100/π` Hz, independente da sobreamostragem**, até o teto `w = 0,625`, que vale
  **`fc_max = 0,625·fs_interno/π`** (8 773 Hz a 44,1 kHz sem sobreamostragem; 17,5 kHz com 2×). Valores sem
  teto: c = 0 → 70 Hz; 0,25 → 443 Hz; 0,5 → 1,44 kHz; 0,75 → 4,1 kHz; 1 → 11,2 kHz.
- O amortecimento `q` (`+0xC` do estágio) vem de `d = 1 − 0,9·r` (rodada 16); há um caminho que o divide por
  `f` e multiplica por `k` antes de processar (opção não identificada).

### 15.2 Modos *alternate* (4 e 5) **[DECOMP `FUN_0085d5a0`, `FUN_00859100`]**

- Nº de estágios: `modo − 1` em `+0xB4`; se `modo − 1 > 2` (modos 4/5) liga-se a flag *alternate* (`+0xB8`) e
  subtrai-se 2 → **modo 4 = 2 estágios, modo 5 = 3 estágios** (os mesmos números de "dual"/"triple").
- Com *alternate*, depois de calcular cada estágio a variável de frequência x (antes do *offset*) é elevada
  a um expoente da tabela `0x8d1e4c` indexado pelo nº de estágios: **E = 0,5 para 2 estágios, 0,75 para 3**.
  Os estágios são calculados do último para o primeiro, então os cortes são `x, x^E, x^(E²)` (x < 1 → os
  estágios seguintes ficam **mais agudos**). Vale para o SVF e para as famílias biquad.
- Em multiestágio, a ressonância/largura é elevada a `1/N` (`+0xBC = 1/(N)`; no Mango, outra fórmula).

### 15.3 Mod Y → volume **[DECOMP render `FUN_0085aa10`]**

- X e Y (0…1024) são suavizados e usados como **índices inteiros (0…1024) em tabelas de 1025 pontos** que
  amostram a curva de cada mapa. Para cada articulador: `saída = base`; para cada mapa presente (X, Y,
  seguidor de envelope × profundidade), conforme o modo de combinação inteiro do articulador:
  **modo 0: `saída ·= mapa(·)`**, modo 1: `saída += mapa(·)`, outros: `saída ·= 2·mapa(·)`. Depois o método
  virtual `+0x38` do articulador é chamado (envelope/LFO; não decompilado).
- O volume da unidade aplicado na saída é **`VOL_unidade · saída_do_articulador_de_volume`** (chamada
  `FUN_00616620(pan = PAN + 2·art_pan, vol = VOL · art_vol)`), e o pan é **somado** (`2·art_pan`).
- Portanto, se o articulador de volume estiver no modo 0 (multiplicativo) com os outros mapas neutros (1,0),
  **o ganho da unidade é multiplicado linearmente por `mapaY(y)`** — com a reta (0,0)→(1,1), ganho = y.
  **Ressalvas:** qual valor do campo inteiro de modo corresponde ao "unipolar" gravado no `.flp` e o que o
  método `+0x38` faz com o envelope/LFO desligados **não foram confirmados**.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1 | Frequency Splitter: barramentos | soma no 0; banda i (0 grave, 1 médio, 2 agudo) no destino gravado nas palavras **15/16/17**; em 2 bandas o médio é **mudo** (agudo continua no par do agudo) **[DECOMP]**; "destino k = barramento k do Patcher" não confirmado no host |
| 1 | Frequency Splitter: inclinação | combo 0…7 = **6/12/18/24/36/48/72/96 dB/oit**; pares = Linkwitz-Riley (Butterworth²), 6 = 1ª ordem, 18 = estrutura com passa-tudo **[DECOMP+DOC]** |
| 1 | Frequency Splitter: níveis | ganho = 2x (x < 0,5) / 1 + 13,8866(x − 0,5); 0,5 = 0 dB, 1 = +18 dB **[DECOMP+DOC]**; freq `30·2^(9,0589x)` **[DECOMP]** |
| 2 | Stereo Shaper: barramento extra | barramento 0 = saída da matriz; barramento do parâmetro 6 = **entrada − saída** (diferença) **[DECOMP+DOC]** |
| 3 | Balance / PanOMatic volume | **confirmado**: 0…320, 256 = 0 dB, `(11^(v/256) − 1)/10`; 320 = +5,6 dB **[DECOMP+DFM]** |
| 4 | X-Y tensão | **`k = 0,1(1001^(|t|/128) − 1)`; t>0: `1 − ((k+1)^(1−p) − 1)/k`; t<0: `((k+1)^p − 1)/k`** **[DECOMP]** |
| 5 | Peak Controller | saídas **0 = Peak+LFO (soma), 1 = Peak, 2 = LFO**; amount `±0,4(6^(|A|/256) − 1)`; tensão como X-Y; ataque instantâneo (pico de bloco); queda **linear** `(10001^(D/128) − 1)·0,0192` por semínima **[DECOMP+DOC]** |
| 6 | Kepler | octave −1/0/+1; A/D/R por tabelas (s); LFO `21,74·2^(−T)` Hz (0,125…21,7); volume cúbico, 1,0 = 0 dB (0,7 = −8 dB); cutoff `2^C(x)` Hz (4 Hz…50 kHz, recorte 5…30 kHz); env→cutoff 0…8,97 oitavas **[DECOMP]**; taxas do chorus **não confirmadas** |
| 7 | BassDrum | peak `100x³` (× 1024/N, N não identificado → **não confirmado** em Hz); slide τ ≈ 5000·(20x³+0,001) amostras; duração linear (x³+0,001)·44100 amostras **[DECOMP]**; clique **não confirmado** |
| 8 | PoiZone | cutoff `2^(10·min(1,1x+mods; 0,91) + 6)` Hz (= 64·2^(11x)); EG ±10·a² oitavas; balance 0 = A, 1 = B; LFO ±10·l² oitavas **[DECOMP+DOC]** |
| 9 | Toxic Biohazard | coarse `trunc(198x − 99)` st (±99); fine ×0,01 st; tempos `max(50x⁴, 0,00101)`; matriz `(⌊100x⌋/100)²` **[DECOMP+DOC]**; índice FM e corte em Hz **não confirmados** |
| 10 | Sakura | **não confirmado** |
| 11 | DX10 | FM real: `φ += D + L·sen(mod)`, `f_m = R·f_c/2`, `L = 0,0002p²(44100/fs)·min(2^(n/12),50)·64` (S = 0) → **β ≈ 69·p²/(R·2^oct) rad ≈ 11·p²/(R·2^oct) ciclos** **[DECOMP]** |
| 12 | Sytrus | quantidade FM = 4·g **[DECOMP]**, unidade de fase **não confirmada**; articulador de corte **bipolar** (0,5 = sem desvio) **[DOC+DECOMP]** |
| 12 | Harmor | volume com articulador desligado e escala do filtro: **não confirmados** |
| 13 | Low Lifter | Chebyshev T₂/T₃ sobre a banda grave com pré-ganho e limitador: `y = (u + aT₂ + a²T₃)/(1 + 2a)`, `a = 10^(Slope/20)` **[DECOMP+DOC]** |
| 14 | Ogun | nível `(3^(P0/128) − 1)/2` (+12 dB no máximo); `s = 44100/fs_int·(½ se fs_int < 88,2k)` → `fc = 22050(c + 0,003)` Hz a 44,1/48 kHz **[DECOMP+DOC]** |
| 15 | Love Philter k / SVF | `k = 44100/fs_interno` confirmado; SVF Chamberlin, `f = 2sen(w)` → `fc = (x+0,005)·44100/π`, teto `0,625·fs_int/π` **[DECOMP]** |
| 16 | Love Philter modos 4/5 | 2/3 estágios com x elevado a 0,5 / 0,75 a cada estágio **[DECOMP]** |
| 17 | Love Philter Mod Y → volume | multiplicativo: `vol = VOL · (base · mapaY(y) · …)` no modo 0 **[DECOMP]**; correspondência modo-inteiro ↔ "unipolar" **não confirmada** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Frequency Splitter:** ler as palavras 15/16/17 (destinos) e mapear barramento k → banda cujo destino é
   k; em 2 bandas, o barramento do médio sai mudo; usar o combo de inclinação (0…7) → 6…96 dB/oit
   (Linkwitz-Riley onde par) em vez de 24 fixo; nível `2x`/`1 + 13,8866(x − 0,5)`.
2. **Stereo Shaper:** no grafo do Patcher, gerar o barramento do parâmetro 6 como `entrada − saída da matriz`.
3. **Peak Controller:** corrigir a ordem das saídas (0 = soma Peak+LFO), usar a tensão exponencial e a queda
   linear por semínima `(10001^(D/128) − 1)·0,0192`.
4. **X-Y Controller:** trocar a curva por `k = 0,1(1001^(|t|/128) − 1)` e as duas formas do §4.
5. **Kepler:** usar as tabelas do §6 (A/D/R, LFO, corte, env→corte) e o volume cúbico (`v = 1` = 0 dB).
6. **PoiZone:** corte `64·2^(11x)` Hz com teto 35 kHz; EG/LFO → corte em `±10·a²` oitavas.
7. **Toxic:** coarse/fine `trunc(198x − 99)` (st / centésimos); tempos `50·x⁴`.
8. **DX10:** profundidade de FM `β ≈ 69·p²/(R·2^oct)` rad com o modulador em `R·f_c/2`.
9. **Low Lifter:** trocar o `+0,25·preBoost` dB pelo *waveshaper* de Chebyshev do §13.
10. **Ogun:** nível `(3^(P0/128) − 1)/2`; corte `22050·(c + 0,003)` Hz (44,1/48 kHz).

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| FSplit motor `FUN_1801e56d0`, `FUN_1801e5c90`, `FUN_1801e7f10` | 4 pares de saída, ordem das bandas, médio zerado em 2 bandas | DECOMP |
| FSplit motor `FUN_1801e6440` (tabela `0x1803024a0`), `FUN_1801e8190` (Q `0x1803022d8…0x180302380`) | 6…96 dB/oit; estruturas Linkwitz-Riley | DECOMP |
| FSplit motor `FUN_1801e6670`, `FUN_1801e6d00` | Hz `30·2^(9,0589x)`; ganho `2x` / `1 + 13,8866(x−0,5)` | DECOMP |
| FSplit wrapper `FUN_007423b0`, `FUN_0073fac0` | envio por banda (`+0x214…`), soma na saída principal; destinos nas palavras 15–17 | DECOMP |
| Stereo Shaper `FUN_006cf460`, `FUN_006ce390` | diferença entrada − saída no envio do parâmetro 6; 8 parâmetros | DECOMP |
| Balance `FUN_006beaa0`, `FUN_006be800`; PanOMatic `FUN_006c6650`, `FUN_005e1bc0`; DFMs | curva `11^x`, 320/256, lei de balanço | DECOMP+STR |
| X-Y `FUN_006c6360`, `FUN_006c6d90` | tensão `1001^`, formas côncava/convexa, saída por eixo | DECOMP |
| Peak `FUN_006fd830` (tabela `0x756180`), `FUN_006fdbc0`, `FUN_006fcef0`, `FUN_006fda30`, `FUN_006fd0e0`, `FUN_006fc6d0`, `FUN_006fd180` | saídas, soma, quantidade `6^`, tensão, detector, queda por PPQ, LFO | DECOMP |
| Kepler `FUN_18001db30`, `FUN_180019a20`, `FUN_180022840`, `FUN_180023ab0`, `FUN_180016490`, tabelas `0x180105b40…0x180105cb8`, `0x1802540a0`, `0x180254100` | tempos, LFO, corte, env→corte, volume, octave | DECOMP |
| BassDrum `FUN_18002ae90`, `FUN_180028ff0`, `FUN_180029440` | peak `100x³`, slide, duração | DECOMP |
| PoiZone `FUN_18002e3f0`, `FUN_1800312e0`, `FUN_18002c8a0`, `FUN_18002cb70`, `FUN_18002ce40` | corte `2^(10v+6)`, EG/LFO quadráticos, balance | DECOMP |
| Toxic `FUN_18003dcc0`, `FUN_18003e730`, `FUN_18003d480`, `FUN_18003d4b0`, `FUN_180040080`, `FUN_18003e510`, `FUN_18003acb0` | coarse/fine, tempos `50x⁴`, matriz, filtro | DECOMP |
| DX10 `FUN_00735760`, `FUN_00735e30`, `FUN_007364a0` | FM no incremento; razão/2; nível do modulador | DECOMP |
| Sytrus `FUN_0086dd40` (`0x86e664/668/66c`) | FM ×4, feedback ×35/128 | DECOMP |
| `ildsp_x64.dll` `FUN_180177a80`, `FUN_180177f50`, `FUN_1801c83b0` (pow) | Faust `lowFreqEnhancementChebyshev`; T₂/T₃, `10^(Slope/20)`, limitador | DECOMP |
| Ogun `FUN_0086a570`, `FUN_00864800`, render (`+0xF3C`), `FUN_00868c70` | nível `3^`, `s` e sobreamostragem | DECOMP |
| Love Philter `FUN_0085d5a0`, `FUN_00854e20`, `FUN_0062b030`, `FUN_00859100`, `FUN_006257c0`, `FUN_0085aa10`, `FUN_00616620` | k, SVF Chamberlin, modos alternate, mapas X/Y, volume | DECOMP |
| Manual oficial (páginas citadas) | faixas, nomes de saída, descrições | DOC |

### Questões em aberto

- Mapeamento "destino de envio k → barramento k do Patcher" (Frequency Splitter, Stereo Shaper): está no host
  (FL.exe), não nos plugins.
- BassDrum: o inteiro N em `1024/N` (escala do *peak*); escalas do clique.
- Toxic: índice de FM e corte em Hz (render do operador e do filtro `FUN_1800406d0`).
- Sytrus: unidade de fase da quantidade FM 4·g (render do operador).
- Harmor: volume com articulador desligado; escala do corte dos filtros.
- Sakura: todas as escalas (DSP no motor).
- Kepler: taxas do chorus I/II; se a taxa da voz `+0x2DF4` inclui sobreamostragem.
- Love Philter: valor do modo inteiro de combinação ↔ "unipolar"; método `+0x38` do articulador.
