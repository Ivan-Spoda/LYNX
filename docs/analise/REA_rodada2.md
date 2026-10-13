# Análise comportamental do FL Studio 2026: rodada 2 (REA)

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md`. É engenharia reversa
> *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está
> aqui são descrições, layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`),
> offsets e globais servem só para rastrear a evidência.

- **Alvo:** `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA
- **Validação de formato:** os 165 `.flp` de `FL Studio 2026/Data` (versões 10.9 … 26.1), lidos por um
  parser próprio de eventos
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `T+0xNN` é um offset no registro de trilha do mixer (array global `G[0x15e06a0]`, stride 0x1478).

---

## 1. Compensação de latência (PDC)

Responde à pergunta em aberto nº 1 do documento principal.

### 1.1 Funções

| Função | Papel |
|---|---|
| `FUN_01100530` | Ponto de entrada do recálculo: snapshot das latências → propagação → compensação → total global **[DECOMP]** |
| `FUN_010fef10` | Snapshot: guarda a latência reportada por cada plugin (gerador ou slot) em `plugin+0xC8`. Plugin bypassado reporta **0** (`FUN_012b6930` / `FUN_013308f0`) **[DECOMP]** |
| `FUN_010ff820` → `FUN_010ff3b0` | Propagação recursiva pelo grafo de roteamento (DFS com memo; o sentinela `0x7FFFFFFF` em `T+0xC4` marca "não calculado") **[DECOMP]** |
| `FUN_010ffca0` | Compensação: calcula o comprimento de todas as linhas de atraso **[DECOMP]** |
| `FUN_01100b10(track, flags)` | Latência em ms para a UI e para o total do master (bit 1 = recursivo pelas entradas, bit 2 = soma dos slots, bit 4 = sem manual-out) **[DECOMP]** |
| `FUN_006a3a90` / `FUN_006a3ac0` | Conversão amostras↔ms **[DECOMP]** |
| `FUN_009289b0` (`TBasicDelay_32FS`) | Linha de atraso **[DECOMP+STR]** |

### 1.2 Campos da trilha usados no PDC **[DECOMP]**

| Campo | Tipo | Significado |
|---|---|---|
| `T+0x1C8` | float ms | **Latência manual de entrada.** Se ≥ 0, funciona como piso: `max(manual, latência dos geradores)`. Se < 0, o módulo é *somado* e a entrada da trilha é atrasada por ele |
| `T+0x1CC` | float ms | **Latência manual de saída**, sempre somada |
| `T+0xBC` | int smp | Latência própria de entrada (geradores roteados + manual) |
| `T+0xB8` | int smp | **Alinhamento de entrada** = `max(T+0xBC, max sobre as fontes S→T de S+0xC4)` |
| `T+0xC0` | int smp | Soma das latências dos slots + manual-out |
| `T+0xC4` | int smp | **Latência de saída** (instante de chegada na saída da trilha) |
| `T+0xA0/0xA8/0xB0` | delay | Linhas de atraso: meter/visual, entrada (manual < 0), saída física |
| `T+0x1D1` | byte (padrão 1) | "Delay the output of this track by the total mixer latency when it has an external output" |
| `T+0x1D0` | byte (padrão 1) | Compensação de latência na gravação de entrada externa |
| `T+0x1472` | tri-estado | Trilha **excluída** do PDC (0 = não, 1 = sim, 2 = condicional) **[DECOMP; nome HIP]** |

Os dois floats de latência manual são exatamente os campos que o evento `0xEC` grava (ver §2.6). O
"campo ofuscado por XOR" do documento principal **não é ofuscação**: em arquivos < 12.9.0 o valor era
gravado com o bit de sinal invertido (XOR `0x80000000`, `DAT_00f50e60`).

### 1.3 Algoritmo de propagação (`FUN_010ff3b0`) **[DECOMP]**

Para cada trilha T, em DFS:

1. `genLat` = maior latência entre os geradores cujo destino é T (campo do canal `+0x258`, ou saídas
   extras do plugin na lista `+0x128`).
2. `T.inOwn = smp(manual ≥ 0 ? max(ms(genLat), manual) : ms(genLat) + |manual|)`.
3. `T.inAlign = max(T.inOwn, max(S.outLat para cada S com rota S→T))`. A rota existe quando o byte
   `S+0x398+T*8` é ≠ 0. Se S ainda não foi resolvida, recursa nela.
4. Se o PDC global está ligado, percorre os slots ativos em ordem acumulando `run`, que começa em
   `inAlign`:
   - `slot.inPos = run`
   - `slot.align = max(run, latência das fontes de *sidechain* que entram nesse plugin)`
   - `run = slot.align + slot.lat`
5. `T.outLat = run + smp(manualOut)` e `T.sumLat = Σslot.lat + smp(manualOut)`.
6. `M` = máximo de `outLat` entre todas as trilhas (exceto a 501), que é a latência total do mixer.

### 1.4 Onde ficam as linhas de atraso: compensação **por aresta** (`FUN_010ffca0`) **[DECOMP]**

O Auto PDC aplica a regra "máximo das entradas menos a latência de cada caminho" **em cada aresta do
grafo**, e não na entrada da trilha:

| Local | Comprimento do atraso |
|---|---|
| **Cada send/rota S→D** (array em `T+0x1344`, entradas de 0x18 B `{dest, level, levelRamp, delay*, tailCounter}`) | `D.inAlign − S.outLat`. Só é criado se > 0, com PDC ligado e a trilha não excluída |
| **Cada saída de gerador ou saída extra de plugin** (lista `TFXTrackSideIOList` em `T+0x128`, entradas de 0x28 B) | `destino.inAlign − (plugin.align + plugin.lat)` |
| **Cada sidechain** (lista em `T+0x130`) | `plugin.align − fonte.outLat` |
| Saída física da trilha (`T+0xB0`) | `M − T.outLat`, se a trilha tem saída externa e `T+0x1D1` |
| Meter/visual (`T+0xA0`) | `M − T.outLat`, só para alinhar o medidor |
| Entrada (`T+0xA8`) | só quando a latência manual de entrada é negativa |

Quando um comprimento muda, a linha é zerada e uma flag de "mudou" é marcada.

**A linha de atraso** (`TBasicDelay_32FS`) é um ring buffer de frames estéreo float32 intercalados
(8 B/frame). O atraso é inteiro em amostras, sem interpolação. Cada amostra é trocada in-place entre o
buffer e o fluxo **[DECOMP]**.

### 1.5 Totais, plugins e opções

- O total em ms é `FUN_01100b10(0, 3)` e vai para `*PTR_DAT_015e2b30`. O total em amostras, incluindo a
  latência de saída do driver, vai para `*PTR_DAT_015e3408` **[DECOMP]**.
- O plugin é informado (opcode 0x1E) do seu deslocamento de entrada `inLat + slot.align` e da latência
  restante até o fim da cadeia **[DECOMP]**. É isso que os plugins *PDC-aware* usam.
- **Hold de cauda:** `T+0x36C = T.inOwn + T.sumLat (+ SR/5 se o EQ da trilha estiver ativo)`. Mantém a
  trilha processando depois que o sinal some, para que as linhas de atraso esvaziem **[DECOMP]**.
- "Compensate for master latency" e "Delay never bypass latency comp": as strings só aparecem em
  DFM/tabelas, sem xref direto. O candidato para a segunda é `T+0x1472` **[HIP]**.

### 1.6 "Trim PDC silence" no export (`FUN_00d18130`, `FUN_00d175b0`) **[DECOMP]**

- No início do render, cada *writer* (um por stem; ver §5) recebe a latência acumulada da sua trilha em
  amostras.
- As primeiras `latência` amostras são **descartadas**. O bloco em que o contador cruza zero é escrito a
  partir do ponto exato.
- Depois do fim da música, o render continua por **mais `latência` amostras**, para o final não ser
  cortado.
- Sem a opção, o contador vale 0 e o silêncio do PDC fica no arquivo.
- Sem split, o master usa contadores globais (`0x15e4d90`/`0x15e3688`).

### Recomendações para o LYNX (PDC)

1. **Modelo por aresta.** Em ordem topológica, calcule `inAlign[T] = max(inOwn[T], outLat[S] ∀ S→T)` e
   `outLat[T] = inAlign[T] + Σlat(slots ativos) + manualOut[T]`. Coloque um `DelayLine` em **cada aresta**
   S→T de comprimento `inAlign[T] − outLat[S]`, e o mesmo para gerador→trilha e para sidechains. O caminho
   mais longo nunca leva atraso.
2. **Plugin bypassado conta como latência 0.** Bypass, adição ou remoção de plugin e mudança de
   `getLatencySamples()` disparam um recálculo fora da thread de áudio. O resultado vai para a thread de
   áudio por swap atômico, e as linhas cujo comprimento mudou são zeradas.
3. A linha de atraso é um ring buffer estéreo float com atraso inteiro. Basta `juce::AudioBuffer` + índice
   circular.
4. Ofereça latência manual por trilha (entrada e saída, em ms) e a opção "alinhar saída física à latência
   total M".
5. No export, descarte `outLat` amostras do início de cada stem e renderize o mesmo tanto a mais no fim.
   Deixe ligado por padrão.
6. Mantenha a trilha viva por `latência + cauda` depois do silêncio (hold), senão o fim do material
   atrasado é cortado.

---

## 2. Mixer: ordem de processamento, sends, pan, solo, smart disable

O processador da trilha é `TMixerTrackModule` (unit `Mixer.TrackModule`, VMT `0x1276868`).
`Process` = `FUN_01276ce0` → **`FUN_01276ee0`** **[DECOMP+STR]**.

### 2.1 Ordem dentro da trilha **[DECOMP]**

Antes das trilhas, em `FUN_00d0d4c0`, cada gerador passa pela sua linha de PDC e é **somado** no buffer da
trilha de destino (sob o lock da trilha), que fica marcada com "tem sinal". Depois, dentro da trilha:

1. **Monitoramento de entrada física** (`FUN_01276b40`): soma a entrada de hardware. Modo em `T+0x30`:
   0 = off, 1 = quando armada, 2 = auto, 3 = sempre.
2. Atualiza "tem sinal" e verifica se há cauda pendente nas linhas de PDC.
3. Tap de gravação (`T+0x2F`): 0 = entrada, 1 = pré-FX, 2 = pós-FX, 3 = pós-EQ, 4 = pós-fader,
   5 = final.
4. **Caminho silencioso:** sem sinal, sem cauda e com hold zerado, a trilha pula tudo, zera a saída e os
   ramps dão *snap* para o alvo.
5. **10 slots em série** (`TPluginRackModule`, `FUN_00cf2a90`). Cada slot faz **mix dry/wet com ramp**
   (parâmetro "mix" do slot, 0..12800).
6. **EQ paramétrico de 3 bandas embutido** (`T+0x1D4 + i*0x88`; freq =
   `10 + 159,9·(100^(x/65536) − 1)` Hz).
7. **Volume / pan / separação estéreo / polaridade / swap**, como **matriz 2×2 com ramp** (§2.2).
8. **Mute** (`T+0x18 == 0` zera) com **fade de mute** (`FUN_01277350`, ≈ 1 ms **[HIP]**).
9. Meter (com atraso de alinhamento `T+0xA0`).
10. **Sends/rotas** (`FUN_01277980`): para cada rota, o ganho é suavizado por bloco (ramp linear com passo
    máximo). Em seguida copia, passa pela linha de PDC da aresta e **soma no destino** sob o lock do
    destino. **Todas as rotas são pós-fader e pós-mute; não há pré-fader neste caminho.**
11. Sidechains (`FUN_01277bd0`), saídas extras de plugins (`FUN_012777e0`) e saída física (`T+0x74`).

Ordem resumida: **entrada → 10 FX (dry/wet) → EQ → fader/pan/sep → mute → meter → sends pós-fader**.

### 2.2 Volume, pan e separação estéreo (`FUN_0127b050`, aplicado em `FUN_006a13a0`) **[DECOMP]**

Campos: `T+0x384` volume (int, **unity = 12800**, máx 16000), `T+0x388` pan (int ±6400),
`T+0x38C` separação (int ±64), `T+0x390` polaridade, `T+0x391` swap L/R.

Saída: `Lout = a·L + c·R`, `Rout = b·R + d·L`.

1. **Curva do fader:** com `v = vol/12800`, `g = (11^v − 1)/10`. Em v = 1, g = 1 (0 dB); em 16000
   (v = 1,25), ≈ +5,6 dB. A polaridade nega g.
2. **Lei de pan** (global, `DAT_015f49a4`):
   - **Potência constante** (padrão): `θ = (1+p)·π/4`, `gL = cos θ·g√2`, `gR = sin θ·g√2`. Dá 0 dB no
     centro e +3 dB no lado para onde se panoreia.
   - **Linear** (modo 1): para p ≥ 0, `gR = g` e `gL = (1−p)·g`, e o simétrico para p < 0.
3. **Fold estéreo:** o canal atenuado não some; metade dele é dobrada para o outro lado. Com
   `f = 1 − g_lado/g`, a matriz recebe `(1 − 0,5f)` direto e `0,5f` cruzado. No hard pan isso dá soma mono.
4. **Separação:**
   - `sep > 0` (estreitar): `s = (5^(sep/64) − 1)/4`. Em +64 o sinal fica mono.
   - `sep < 0` (alargar): `s = sep/64`. Em −64, `L' = 1,5L − 0,5R` (Side ×2).
   - `s` é somado ao `f` dos dois lados, com clamp ≤ 1.
5. **Swap** troca as linhas da matriz. Fast path: unity + centro + sep 0 = identidade.

### 2.3 Solo e mute **[DECOMP]**

- Bytes: `T+0x18` enabled, `T+0x1A` solo, `T+0x1B` *mute-lock*, `T+0x1C` estado salvo.
- **Solo não é uma máscara de áudio.** Ele reescreve o `enabled` de todas as trilhas (`FUN_0127c2a0` →
  `FUN_0127bb10`). Uma trilha T fica ativa se:
  - T é a trilha solada, ou
  - (modo & 2) e **T roteia transitivamente para a solada** (fontes a montante), ou
  - (modo & 1) e **a solada roteia transitivamente para T** (destinos a jusante, ex.: master/bus).
- O fecho transitivo é uma DFS sobre os bytes de rota (`FUN_0127c640`/`FUN_0127c6f0`). O modo padrão
  (API) é 2, que inclui as fontes.
- Trilhas com mute-lock não mudam.
- Ao tirar o solo, restaura o estado salvo (se uma opção global estiver ligada) ou reativa tudo.

**Resposta à pergunta:** sim, o solo mantém audíveis as trilhas que roteiam *para* a solada. O caminho
até o master continua funcionando porque o próprio master não é mutado.

### 2.4 Smart disable (`FUN_00cf2f90` / `FUN_00cf3020`) **[DECOMP]**

- Só vale se o plugin tem "Allow smart disable" (`plugin+0xF7`).
- Teste de silêncio: todas as amostras do bloco (L e R) com |x| ≤ **1,5849e-5 (≈ −96 dBFS)**.
- Com entrada silenciosa, o contador acumula o tamanho do bloco. Ao chegar em **SR × 4 (4 s de hold)**,
  o slot é **pulado**.
- O contador é zerado se a entrada **ou a saída** não estiverem em silêncio, o que preserva caudas de
  reverb e delay.
- A trilha inteira só para de processar quando não tem sinal **e** o hold de latência/cauda (`T+0x36C`)
  zerou.

### 2.5 Roteamento: evento `0xEB` **[DECOMP+DADOS]** (dispatcher `FUN_00f46490`)

- O payload é um **array de bytes booleanos**, um por trilha de destino, truncado no último destino
  usado. Tamanhos vistos: 1…39 B.
- O byte i ≠ 0 significa "esta trilha envia para a trilha i". Vai para `T+0x398 + i*8`.
- **O nível do send não está aqui.** Ele está no evento `0xE1` (§2.7).
- Remapeamento de índices em arquivos antigos:
  - `< 8.5.0`: destino > 64 soma 35.
  - `< 12.9.0`: destino > 103 soma 397.
  - `< 24.2.99`, ou sem a flag de arquivo `+0x4447`: destino > 125 soma 375.
  - Assim os antigos "Current"/extras caem em 500/501.

### 2.6 Flags e latência da trilha: evento `0xEC` **[DECOMP+DADOS]**

Payload de **12 bytes** em versões ≥ 12.9.0 (5 bytes em arquivos muito antigos): `f32 latOut_ms`,
`u32 flags`, `f32 latIn_ms`.

| Bit de `flags` | Campo | Significado |
|---|---|---|
| 0x0001 | `T+0x390` | Polaridade invertida |
| 0x0002 | `T+0x391` | Swap L/R |
| 0x0004 | `T+0x13F5` | ? (quase sempre ligado) |
| 0x0008 | `FUN_012c1810` → `T+0x18` | **Habilitada** (bit apagado = mutada) |
| 0x0010 | `T+0x380 = !bit` | **Efeitos desligados** (os 10 slots) |
| 0x00C0 | `T+0x2D` | Dock: 0 = esquerda, 1 = meio, 2 = direita |
| 0x0400 | `T+0x2C` | Separador visual |
| 0x0800 | `T+0x1B` | Mute-lock |
| 0x1000 | `T+0x1A` | Solo |
| 0x2000 | `T+0x2E` | ? Em arquivos < 20.9 define o tap de gravação (1 se ligado, senão 3/5) **[HIP: "audio track/armada"]** |
| 0x4000 | `T+0x1D1 = !bit` | **Não** atrasar a saída física pela latência total |
| 0x8000 | `T+0x1D0 = !bit` | **Não** compensar a latência de gravação |

Valores mais comuns nos demos: `0x4C` (habilitada + dock meio + bit 4), `0x44C` (com separador), `0x204C`.
Em arquivos < 12.9.0 havia um só float de latência com o sinal invertido; o bit 0x20 escolhia se ele era
de saída ou de entrada.

### 2.7 Parâmetros do mixer e níveis de send: evento `0xE1` + IDs de controle **[DECOMP+DADOS]**

`0xE1` é um array de registros de **12 bytes** `{u32 reservado, u32 controlID, i32 valor}` (o mesmo
formato dos eventos de controle `0xD8`/`0xDF`, tipo 0 em `FUN_00f558e0`). No fim do load, cada registro é
aplicado com `FUN_01046a70(controlID, valor, 0x10011)`, o **setter universal de parâmetro automatizável**.

**Codificação do `controlID` (u32)**, que é o mesmo endereço usado pelos links de automação (§3.3):

| Faixa | Alvo |
|---|---|
| `< 0x10000000` | Canal do Channel Rack: `canal = id>>16`, `param = id & 0xFFFF` (0…12 = parâmetros genéricos do canal; 0x10…0x14, 0x103… etc.) |
| `0x40000000 + n` | Parâmetros globais (tempo, master pitch…) **[HIP para cada n]** |
| `0x70000000 + (trilha << 22) + (slot << 16) + p` | **Mixer**: trilha 0…501, slot 0…9 |

Valores de `p` para o mixer:

| `p` | Significado | Faixa / padrão |
|---|---|---|
| `0x1F00` | Slot ligado | 0/1 |
| `0x1F01` | Mix (wet) do slot | 0…12800, unity 12800 |
| `0x1FC0` | Volume da trilha | unity 12800, máx 16000 |
| `0x1FC1` | Pan | ±6400 |
| `0x1FC2` | Separação estéreo | ±64 |
| `0x1FD0…0x1FD2` | Ganho das 3 bandas do EQ | |
| `0x1FD8…0x1FDA` | Frequência das 3 bandas | |
| `0x1FE0…0x1FE2` | Q das 3 bandas | |
| `0x2000 + d` | **Nível do send para a trilha d** (d até 0x1FF) | 0…16000, unity 12800 |
| `0x8000 + k` | Parâmetro k do plugin no slot | |

- Em arquivos < 24.2.99 a classe do mixer era `0x2…` com trilha em 6 bits. O conversor `FUN_00f45460`
  soma `0x50000000`, move os sends `0x1F40+d` para `0x2000+d` e manda a antiga trilha 126 para a 501.
- Na memória, o send fica em `T+0x394 + d*8 = {i32 level, u8 enabled}` **[DECOMP]**.
- Com a flag `0x20` no setter, o valor chega **normalizado** (0…2³⁰) e é mapeado linearmente para
  `[min, max]` do parâmetro. É esse o caminho da automação **[DECOMP]**.

**Ordem dos eventos de uma trilha no FLP [DADOS]:** `0x95` (cor), `0x5F` (ícone), `0xCC` (nome),
`0xEC` (flags/latência), 10× [plugin opcional: `0xC9` nome padrão, `0xD4` cabeçalho, `0xCB` nome,
`0x80` cor, `0xD5` estado] seguido de `0x62` (índice do slot), depois `0xEB` (rotas), `0xA5`, `0xA6`,
`0x31`, `0x9A` (entrada física), `0x93` (saída física). O registro da trilha **fecha** em `0x93`. O
master não tem nome; o `0xCC` que vem logo depois dele já pertence à trilha 1.

### Recomendações para o LYNX (mixer)

1. Ordem fixa: **soma de entradas → 10 slots (cada um com dry/wet) → EQ de 3 bandas opcional → matriz
   2×2 (vol/pan/sep/fase/swap) com ramp → mute com fade de ~1 ms → meters → sends pós-fader**. Se for
   oferecer send pré-fader, é extensão nossa; o FL não tem.
2. Fader: `g = (11^v − 1)/10`, com `v = valor/12800` ∈ [0; 1,25] e unity em 0,8 do curso (12800/16000).
3. Duas leis de pan: "constant power" (sin/cos × √2) e "linear". Implemente o fold de 50% do canal
   atenuado, porque é isso que dá o som característico do pan do FL.
4. Separação: fórmula exponencial para estreitar e linear para alargar.
5. Solo: calcule `enabled` pelo fecho transitivo do grafo, incluindo as fontes por padrão. Respeite o
   mute-lock e salve o estado para restaurar.
6. Smart disable: −96 dBFS, hold de 4 s, testando entrada **e** saída.
7. Importador FLP: rotas vêm de `0xEB` (booleanos) e os níveis vêm de `0xE1` com `p = 0x2000+d`. Faça o
   parse do `controlID` exatamente como na tabela acima; ele também serve para resolver os alvos da
   automação.

---

## 3. Automation clips

### 3.1 Onde ficam no FLP **[DECOMP+DADOS]**

- Cada automation clip é um **canal do Channel Rack** com `ChanType` (evento `0x15`) = **5**. Os tipos
  vistos nos demos são 2 = plugin gerador, 4 = audio clip, 5 = automation clip.
- Os pontos vêm no evento **`0xEA`** dentro do bloco do canal. O load passa o blob ao objeto de
  automação do canal (`canal+0x360`, método virtual +0x10 = stream). O save é `FUN_00f3e190`, que
  serializa esse objeto e grava `0xEA` **[DECOMP]**.
- O **alvo** vem no evento **`0xE3`** (registro de 20 B, um por link; vários links por clip são
  possíveis) **[DADOS]**:

| Offset | Tipo | Campo |
|---|---|---|
| 0 | u32 | `(canal_do_clip << 16)`, onde o u16 alto é o índice do canal de automação |
| 4 | u32 | 0 |
| 8 | u32 | **`controlID` do alvo**, mesma codificação da §2.7 (ex.: `0x72C51F01` = mix do slot 5 da trilha 11; `0x73068001` = parâmetro 1 do plugin no slot 6 da trilha 12; `0x000D0004` = parâmetro 4 do canal 13) |
| 12 | u32 | Flags (8 nos links de automation clip) |
| 16 | u32 | ? (constante `0x1D5` nos demos) |

- O evento `0xE2` tem o mesmo layout com flags 4. São os links de **controle remoto/MIDI** (ex.: campo 0 =
  número do controlador). Ambos vão para listas globais (`PTR_DAT_015e0a00[id−0xE2]`) **[DECOMP]**.

### 3.2 Layout do evento `0xEA` **[DADOS + DECOMP do registro de ponto]**

```
Offset  Tam   Conteúdo
0       4     u32 = 1     (versão?)
4       4     u32 = 0x40
8       4     u32 = 0x400 ou 0x401   (flags?)
12      1     u8  = 0
13      4     u32 = 3
17      4     u32 N = número de pontos
21      24·N  pontos
21+24N  112   bloco final (configuração de LFO/clip; igual em todos os clips de um projeto) [HIP]
```

Tamanho total = 133 + 24·N. Nos demos aparecem 157, 181, 205… **[DADOS]**.

**Ponto (24 B)**, mesmo layout de ponto da família `TCustomMEnvelope` (`FUN_00b97c40`):

| Offset | Tipo | Campo |
|---|---|---|
| 0 | f64 | **Δx: distância até o ponto anterior** (não é posição absoluta). O primeiro ponto tem Δx = 0 |
| 8 | f64 | **Valor y normalizado, 0…1** |
| 16 | f32 | **Tensão**, −1…+1 |
| 20 | u8 | **Tipo de curva** do segmento que *termina* neste ponto (ver tabela) |
| 21 | u8 | Selecionado (zerado no load) |
| 22 | u8 | ? |
| 23 | i8 | Direção, um cache derivado: 0, +1 (tensão > 0), −1 (tensão < 0), 2 (reta) |

- Posições absolutas = soma acumulada de Δx. Nos demos, Δx tem frações de 1/96 (PPQ 96), o que indica
  **unidade de beats (semínimas)** **[HIP forte]**.
- O byte de direção é recalculado no load (`FUN_00b9ba60`) a partir do sinal da tensão. Se a tensão é 0
  e o tipo é uma curva, vira 2 (reta). Não precisa ser confiável no arquivo.
- Na memória, cada ponto ocupa 0x20 B. Os campos `+0x10/+0x14/+0x18` são coeficientes pré-calculados a
  partir da tensão e `+0x1C` é a posição relativa **[DECOMP]**.

### 3.3 Tipos de curva e fórmulas (`FUN_00b9def0`, coeficientes em `FUN_00b9ba60`) **[DECOMP+STR]**

Os nomes vêm do menu `PtCurveNMenu` (DFM). `t` ∈ [0,1] é a fase dentro do segmento, `y0` e `y1` os
valores das pontas e `v = y0 + F(t)·(y1 − y0)`:

| Tipo | Nome | F(t), descrição nossa |
|---|---|---|
| 0 | Single curve | Exponencial: `k = |tensão|·ln(≈10⁶)`, `F = (e^{k t} − 1)/(e^k − 1)` com tensão < 0, e o espelho `1 − (e^{k(1−t)} − 1)/(e^k − 1)` com tensão > 0. Tensão 0 → reta |
| 1 | Double curve | A mesma curva aplicada em duas metades simétricas (S) |
| 2 | Hold | F = 0: mantém y0 até o próximo ponto (degrau) |
| 3 | Stairs | Degraus: `round(t·n − offset)/n`, com n de degraus derivado da tensão |
| 4 | Smooth stairs | Degraus com transição cúbica entre eles |
| 5 | Pulse | Onda quadrada entre y0 e y1, com nº de ciclos derivado da tensão |
| 6 | Wave | Senoide (ou triângulo, conforme o sinal) entre y0 e y1, com ciclos pela tensão |
| 7 | Single curve 2 | Curva polinomial: `t − (u⁶ − u)·tensão`, com u = t ou 1−t conforme o sinal |
| 8 | Double curve 2 | Versão em duas metades do tipo 7 |
| 9 | Half sine | Mistura entre reta e meio seno, com peso = tensão |
| 10 | Smooth | Hermite cúbico usando as inclinações dos segmentos vizinhos (spline suave através dos pontos) |
| 11 | Single curve 3 | Hiperbólica: `A/(1 − B·t) − C`, com B derivado de `1 − (1 − 0,9|tensão|)³` |
| 12 | Double curve 3 | Versão em duas metades do tipo 11 |

Nos demos todos os pontos de clip usam o tipo 0 com tensões variadas **[DADOS]**. Para importar, basta
implementar 0, 1, 2 e 10; os demais podem cair em reta como fallback.

### 3.4 Agendamento na reprodução **[DECOMP]**

- O clip na playlist gera uma **voz** `TMixVoiceAutomationClip` (unit `Mixer.Voices`, VMT `0xd1ff48`),
  com pool próprio (`TDynamicVoiceArray<…AutomationClip>`).
- **Início** (`FUN_00d2a930`):
  - o comprimento é `fimOffset − inícioOffset` do item da playlist (floats do item, §4.2);
  - posiciona o envelope em `inícioOffset / velocidade`.
- **Por sub-bloco** (`FUN_00d24d80`): `elapsed += nAmostras × velocidade`, onde a velocidade
  (amostras → unidades do envelope) é um double do objeto de automação, recalculado com o tempo.
  - Enquanto `elapsed < comprimento`, avança o estado do envelope (método virtual +0x38), que interpola
    o segmento corrente.
  - Quando passa, a voz termina (`FUN_00d244e0`).
- O avanço do envelope (`FUN_00b9def0`) cruza fronteiras de ponto dentro de um bloco, recalcula a taxa do
  segmento como `velocidade/Δx` e trata laço e sustain (índices `+0x4C/+0x50/+0x54` do envelope).
- O valor resultante vai para o alvo pelo setter universal `FUN_01046a70(controlID, valor, flags)`. Com a
  flag `0x20` o valor é normalizado (×2³⁰) e mapeado para `[min, max]` do parâmetro.
- **Granularidade:** a automação é aplicada nas fronteiras de sub-bloco. O motor fatia o buffer nos
  ticks (ver documento principal §1.1), então a resolução efetiva é de **1 tick**.

### Recomendações para o LYNX (automação)

1. **Modelo:** `AutomationClip { vector<Ponto{double dx_beats; double y01; float tensao; uint8 tipo;}>;
   vector<ControlID> alvos; }`. Em memória use posições absolutas e converta para Δx só no import/export.
2. **Endereço de alvo único (`ControlID`)** no estilo FL (classe | trilha | slot | parâmetro). Isso
   simplifica serialização, links de automação, MIDI learn e o importador de FLP ao mesmo tempo.
3. **Valores normalizados 0…1** em toda a automação, mapeados para a faixa do parâmetro no setter. O FL
   faz isso para todo parâmetro, inclusive volume, que é 0…16000 com unity 12800.
4. **Avaliação:** pré-calcule os coeficientes por segmento ao editar (como `FUN_00b9ba60`) e avalie por
   bloco ou sub-bloco. Comece por Single curve (exponencial), Double curve, Hold e Smooth.
5. **Agendamento:** um "voice" por clip ativo, avançando `nAmostras × (beats/amostra)` e aplicando o
   valor no início de cada sub-bloco. Para plugins com parameter automation sample-accurate, emita os
   pontos com offset dentro do bloco.
6. **Importador FLP:** canal com ChanType 5 → clip; pontos de `0xEA` (header de 21 B + 24·N + 112);
   alvos de `0xE3` (`controlID` em +8); posição e comprimento na playlist (§4.2).

---

## 4. Registros de nota e de playlist nas versões novas

### 4.1 Nota: evento `0xE0`, **24 bytes em todas as versões** **[DECOMP+DADOS]**

Nos 165 demos, **todos** os eventos `0xE0` têm tamanho múltiplo de 24, inclusive os de 24.x, 25.x e 26.x.
O mapeamento abaixo vem da conversão nota→objeto da API de scripts do Piano Roll (`FUN_00c3ed10`, nomes
`number time length pan velocity color fcut fres pitchofs slide porta group muted selected release repeats`)
**[DECOMP]**:

| Off | Tipo | Campo | Faixa / padrão |
|---|---|---|---|
| 0x00 | u32 | Posição (ticks) | |
| 0x04 | u16 | Flags: **slide = `(flags & 0xF) == 8`**. O bit 0x4000 está sempre ligado em arquivos novos | `0x4000` / `0x4008` |
| 0x06 | u16 | Índice do canal no rack | |
| 0x08 | u32 | Duração (ticks) | |
| 0x0C | u16 | Tecla (`number`) | 0…131 |
| 0x0E | u16 | Grupo (`group`, id de agrupamento de notas) | 0 |
| 0x10 | u8 | Fine pitch: `pitchofs = valor − 120` | 120 = centro |
| 0x11 | u8 | `repeats` (repetições/ratchet) | 0 |
| 0x12 | u8 | Release (/128) | 64 |
| 0x13 | u8 | bits 0–3 **cor/canal MIDI (0…15)**; 0x10 **porta**; 0x20 **muted**; 0x80 **selected** | |
| 0x14 | u8 | Pan (/128) | 64 |
| 0x15 | u8 | Velocity (/128) | 100 |
| 0x16 | u8 | Mod X / "fcut" (/255) | 128 |
| 0x17 | u8 | Mod Y / "fres" (/255) | 128 |

Conversões de arquivos antigos no load **[DECOMP]**:
- `< 3.1.6`: fine pitch = 120.
- `< 5.3.0`: o nibble baixo de `+0x13` é deslocado `<<2`.
- `< 6.5.0`: release = 64.

**[DADOS]** Nos demos ≥ 24: flags `0x4000` (13.282 notas) e `0x4008` (slide, 156); `+0x13` com 0x20
(muted, 364) e 0x80 (selected, 72); `repeats` com valores 4, 10, 11, 12, 13.

### 4.2 Item de playlist: evento `0xE9` **[DECOMP+DADOS]**

O tamanho do registro (`FUN_00f558e0`, tipo 2) é decidido pela versão e por um **u32 de flags do arquivo
no evento `0xA9`**:

| Condição | Tamanho |
|---|---|
| versão < 20.99.1 | 32 B |
| ≥ 20.99.1 e `(flagsA9 & 2) == 0` (inclui arquivos sem `0xA9`) | **60 B** |
| ≥ 20.99.1, bit 2 ligado e `(flagsA9 & 8) == 0` | **80 B** |
| ≥ 20.99.1, bits 2 e 8 ligados | tamanho nativo atual (≥ 0x58 = 88 B **[HIP]**) |

Nos demos: sem `0xA9` → 60 B; `0xA9 = 7` → 80 B (24.2…25.2); `0xA9 = 15` (25.2.99/26.1) → nativo,
mas os dois arquivos estão vazios **[DADOS]**. O bit 1 do `0xA9` também converte um campo de canal
int→float.

Layout (validado em 10.576 registros de 80 B):

| Off | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | Posição (ticks) |
| 0x04 | u16 | `0x5000` (base) |
| 0x06 | u16 | **Item:** < 0x5000 → índice do canal (audio clip ou automation clip); ≥ 0x5000 → pattern `idx − 0x5000` |
| 0x08 | u32 | Comprimento (ticks) |
| 0x0C | u16 | Trilha da playlist, invertida: `trilha = 499 − valor` **[HIP pelo valor; o load soma +100 em arquivos < 9.?]** |
| 0x0E | u16 | Grupo |
| 0x10 | u16 | 120 (fine pitch padrão do clip) |
| 0x12 | u16 | Flags: 0x0040 padrão; **0x2000 = mutado/desabilitado** **[HIP]** (205 casos) |
| 0x14 | 4×u8 | 64, 100, 128, 128: pan, vel, modX, modY padrão do clip |
| 0x18 | f32 | **Início do offset** (−1 = nenhum). Em automation clips, é o ponto inicial dentro do envelope |
| 0x1C | f32 | **Fim do offset** (−1 = nenhum) |
| 0x20 | u32 | **ID único do clip**, regenerado no load por contador atômico (`FUN_0105f2e0`) |
| 0x24 | f32 | Fade-in? Zerado no load quando `(flagsA9 & 4) == 0` e o item é de um tipo específico **[HIP]** |
| 0x28 | f32 | Tensão do fade-in? **[HIP]** |
| 0x2C | f32 | Fade-out? **[HIP]** |
| 0x30 | f32 | Tensão do fade-out? **[HIP]** |
| 0x34 | f32 | Ganho do clip, linear (padrão 1,0) **[HIP forte]** |
| 0x38 | u32 | Flags de fade/crossfade (0…3, `0x8008`, `0x800A`) **[HIP]** |
| **60–79: só no registro de 80 B** | | Inicializado em `FUN_01064e20` quando ausente |
| 0x3C | u32 | 0 |
| 0x40 | f64 | **1,0** por padrão: fator de tempo/stretch do clip **[HIP]** |
| 0x48 | u32 | 0 |
| 0x4C | i32 | −1 (no nativo; ignorado no de 80 B) |
| 0x50 | u32 | 0 (só no nativo) |

### 4.3 Trilha da playlist: evento `0xEE` (70 B) + nome em `0xEF` **[DECOMP+DADOS]**

- Há 500 registros por arquivo, cada um seguido de um `0x2B` (u8).
- Campos:
  - `u32 índice` (1…500)
  - `u32 cor` (se for `0x565148`, a trilha usa a cor padrão)
  - `u32 ícone`
  - `u8 habilitada`
  - `f32 altura` (1,0)
  - `f32` (−32 / −16) em +0x11
  - `u8` em +0x15
  - bloco em +0x16
  - u8 em +0x2E…+0x30
  - u32 em +0x31/+0x35/+0x39
  - u8 em +0x3D
  - bit 0 de +0x42
- O load aceita versões mais curtas do registro, conforme o tamanho.

### Recomendações para o LYNX (formato)

1. Nota: 24 B fixos. Implemente slide (`flags&0xF==8`), porta (bit 0x10 de +0x13), muted (0x20), cor
   (nibble baixo) e repeats (+0x11). No `flpdump`, imprima esses campos para validar.
2. Playlist: escolha o tamanho do registro por **versão + `0xA9`**, e não por "tamanho÷N", porque 80×3 e
   60×4 são ambíguos (os dois dão 240). Os testes de regressão devem cobrir os 3 tamanhos.
3. Item ≥ 0x5000 = pattern; < 0x5000 = canal (clip de áudio ou de automação).
4. Para clips de automação, use os offsets de início e fim (+0x18/+0x1C) para recortar o envelope.

---

## 5. Diálogo de export / render

Fonte: formulários `TRenderForm` (novo) e `TWAVRenderForm` (antigo, lido pelo driver de render
`FUN_00d18130`) decodificados dos DFM **[STR]**, mais a decompilação do render **[DECOMP]**.

### 5.1 Opções (valor padrão entre parênteses) **[STR]**

| Grupo | Opções |
|---|---|
| Mode | Song, Pattern |
| **Tail** | **Cut remainder** (0, padrão), **Leave remainder** (1), **Wrap remainder** (2) |
| Processamento | Enable master effects (on), Enable insert effects (on), **Trim PDC silence (on)**, Split mixer tracks (off; sub-opções "Include Master" (on) e "Include Current" (off)) |
| Quality: resampling | 2-point linear, 6-point Hermite, 16/24/32/64/128/256/512-point sinc (grupos Low/Average/High) |
| Quality: outros | Dithering (off, "Dithered 16Bit output"), HQ for all plugins (on), Disable maximum polyphony (on) |
| Formato | WAV (16 int / 24 int / 32 float), FLAC (16/24, compressão 0–8, padrão 5), MP3 (bitrate 0–13), OGG (0–15), M4A |
| Canais | Stereo, Mono (merged), Mono (left only), Mono (right only) |
| Extras | Tempo info (chunk tipo ACID), note markers, loop markers (on), playlist markers (on) |
| **Mastering** | No lugar da normalização: alvo LUFS de −25 a −8, padrão −9; presets por plataforma (Spotify, Apple Music, YouTube, CD…); exige FL Cloud |

**Não existe normalização por pico no render de 2026** **[STR]**. "Normalize" aparece só em clips, no
Edison e em knobs.

### 5.2 Comportamento **[DECOMP]**

- **Cut (0):** no tick em que a música acaba (`FUN_00d0e840`), o estado vai para "parar". Só os
  contadores de PDC continuam rodando.
- **Leave (1):** depois do fim, segue renderizando a cauda até o **primeiro bloco em que todo o master
  está abaixo de ≈ −96 dBFS de pico** (`FUN_00d0eb10`/`FUN_00c4d260`; limiar `exp(k·−96)`), com **teto de
  30 s** (constante 30000 ms).
- **Wrap (2):** **não** é mixar a cauda sobre o início do arquivo. É **pré-roll**:
  - o render começa com a escrita desligada;
  - no primeiro fim, a escrita liga (`FUN_00d14880`) e o loop volta ao início;
  - o arquivo recebe a **segunda passada**, que já contém as caudas da primeira;
  - o segundo fim corta.
- **Trim PDC:** ver §1.6.
- **Qualidade** (`FUN_01366a70`): grava o modo de interpolação em todos os canais. Com "HQ for all
  plugins", usa as flags 0x13 em vez de 0x11.
- **Disable max poly:** salva o limite de polifonia de cada canal, zera (ilimitado) e restaura no fim.
- **Master/insert FX:** liga ou desliga os efeitos via `FUN_0127c3e0`.
- **Split:**
  - um writer por trilha (1…N; master e Current opcionais);
  - nome do arquivo = base + nome da trilha;
  - o writer fica no registro do mixer em `+0x118`.
- **Mono:** merged soma (provavelmente (L+R)/2 **[HIP]**); left/right extraem um canal.
- **Dither** (`FUN_012c7510`), só no caminho de **16 bits**: TPDF (diferença de dois uniformes,
  ±1 LSB) com **noise shaping de 2ª ordem por realimentação de erro**,
  `tmp = x·32767 + 0,5·(2e₁ − e₂)`, depois `q = round(tmp + ruído)`, `e₂ = e₁`, `e₁ = tmp − q`, e
  saturação. Sem dither, só faz clamp e escala por 32767.

### Recomendações para o LYNX (export)

1. Implemente o **Wrap como pré-roll**: renderize uma passada sem gravar e grave a segunda. É simples e
   fiel ao FL.
2. Leave: pare no primeiro bloco com pico < −96 dBFS, com teto de 30 s.
3. Trim PDC por stem, ligado por padrão (§1.6).
4. Dither TPDF + shaping de 2ª ordem apenas para inteiros de 16 bits. Guarde o erro em float.
5. Durante o render, troque a interpolação dos samplers para a qualidade escolhida, ligue o HQ dos
   plugins e desligue os limites de polifonia; restaure tudo no fim.
6. Normalização é opcional. Se for feita, use um pico em dois passes. O FL atual usa um alvo LUFS, mas
   depende do serviço de nuvem.

---

## 6. Piano roll: parâmetros das ferramentas **[STR]**

Só o que foi barato extrair dos formulários. Não há algoritmos decompilados aqui.

- **Propriedades de nota:** pitch fino 0…240 (120 = centro), Mod X/Y 0…255 (128), release 0…128 (64),
  vel 0…128 (100), pan 0…128 (64), slide, porta, cor/canal MIDI 1…16, repeat 0…96.
- **Quantize:**
  - strength por propriedade (pan, vel, modX, rel, modY, pitch) 0…128 (128);
  - start 0…128 (64), duration 0…128 (64), sensibilidade/janela 12…192 (36);
  - modos de duração: Leave duration, Leave end time, Quantize duration, Quantize end time;
  - template de groove;
  - atalhos "Quick quantize" e "Quick quantize start times".
- **Arpeggiator:**
  - range Normal/Flip/Alternate, 0…6 oitavas;
  - sync Time/Block/Chord;
  - time mul 1…7, gate 0…48, "Group notes".
- **Chop:** pattern, time mul, Absolute; chop rápido em bars/beats/1/2…1/32.
- **Flam:** time −192…192 (−64), vel 0…128 (85); presets Flam/Early flam/Double stroke.
- **Articulator (Legato):**
  - multiply 10…100%, variation, gap 0…240 ticks, chop chords;
  - presets Legato/Portato/Staccato.
- **Strumizer:** start/vel/end com strength e tension; preserve end; trigger ahead.
- **Randomizer:** níveis ±128 bipolar, seed, escala/acorde, octave, range, population, length,
  "Glue same notes", "Random portamento".
- **Level scaling:** center, multiply (8192 = 1,0), offset, tension.
- **Outras:** Flip, Claw machine, LFO (seno/tri/quadrada, lin/log), Fill (Euclidean).

Recomendação: para o LYNX, priorize Quantize (start + duration + strength + janela) e o editor de
eventos com as propriedades acima. Arp, Strum, Flam e Chop podem ser implementados depois como
transformações puras sobre a lista de notas.

---

## 7. Perguntas que continuam em aberto

1. O significado exato do header (21 B) e do trailer (112 B) do `0xEA`, e a classe do objeto
   `canal+0x360`. O ponto é idêntico ao `TCustomMEnvelope`, mas o header não bate com o stream de
   `TMEnvelope`.
2. Campos 0x24…0x38 do item de playlist (fades e ganho do clip): confirmar por decompilação.
3. "Compensate for master latency", "Delay never bypass latency comp" e o valor de UI que corresponde a
   cada lei de pan.
4. A constante exata do limiar de −96 dB do tail (lida em runtime) e o posicionamento do pré-roll do Wrap.
5. Bit 0x4000 nas flags de nota, bit 0x4 do `0xEC` e o campo +16 (`0x1D5`) dos links de automação.

## Apêndice B: funções novas (rastreabilidade)

| Endereço | Papel inferido | Confiança |
|---|---|---|
| `FUN_01100530` | Recalcula o PDC (snapshot → propagação → compensação) | DECOMP |
| `FUN_010ff3b0` | Propagação de latência por trilha (inAlign/outLat) | DECOMP |
| `FUN_010ffca0` | Comprimentos das linhas de atraso por aresta | DECOMP |
| `FUN_01100b10` | Latência em ms da trilha/master | DECOMP |
| `FUN_009289b0` | `TBasicDelay_32FS` (ring buffer estéreo) | DECOMP |
| `FUN_01276ee0` | `TMixerTrackModule.Process`: ordem interna da trilha | DECOMP |
| `FUN_0127b050` / `FUN_006a13a0` | Matriz vol/pan/sep/fase/swap e aplicação | DECOMP |
| `FUN_01277980` | Sends pós-fader com ramp + PDC por aresta | DECOMP |
| `FUN_0127bb10` | Solo por fecho transitivo do grafo | DECOMP |
| `FUN_00cf2f90` / `FUN_00cf3020` | Smart disable (−96 dB, hold de 4 s) | DECOMP |
| `FUN_01046a70` | Setter universal por `controlID` (load de `0xE1`, automação, MIDI) | DECOMP |
| `FUN_00f45460` | Converte `controlID` antigos do mixer para o esquema de 500 trilhas | DECOMP |
| `FUN_00f558e0` | Tamanho do registro de arrays (nota 24, controle 12, playlist 32/60/80/nativo) | DECOMP |
| `FUN_00f55690` | Pós-load da playlist (IDs de clip, defaults das extensões) | DECOMP |
| `FUN_01064e20` | Defaults do bloco estendido do item de playlist (+0x3C…) | DECOMP |
| `FUN_00c3ed10` | Nota (24 B) → objeto da API de script (mapa de campos) | DECOMP |
| `FUN_00b97c40` | Stream de `TCustomMEnvelope` (pontos de 24 B) | DECOMP |
| `FUN_00b9ba60` | Pré-cálculo dos coeficientes de curva por ponto | DECOMP |
| `FUN_00b9def0` | Avanço e avaliação do envelope (13 tipos de curva) | DECOMP |
| `FUN_00d24d80` / `FUN_00d2a930` | `TMixVoiceAutomationClip`: avanço por bloco e início com offset | DECOMP |
| `FUN_00f3e190` | Save do canal (grava `0xEA`) | DECOMP |
| `FUN_00d18130` / `FUN_00d175b0` | Driver de render: tail, Trim PDC, split | DECOMP |
| `FUN_012c7510` | Dither TPDF com shaping de 2ª ordem (16 bits) | DECOMP |
| `FUN_01366a70` | Qualidade de interpolação e HQ no render | DECOMP |
