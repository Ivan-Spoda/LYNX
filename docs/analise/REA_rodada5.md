# Análise comportamental do FL Studio 2026: rodada 5 (REA) — plugins de terceiros no "Fruity Wrapper"

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md` e de `REA_rodada2.md` …
> `REA_rodada4.md`. Engenharia reversa *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo
> da Image-Line foi copiado: o que está aqui são descrições de formato e de comportamento escritas com
> palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a evidência.

- **Alvos:**
  - `Plugins/Fruity/Generators/Fruity Wrapper/Fruity Wrapper_x64.dll` (SHA-256 `fa5a2f6f…d2e7`, Delphi x64),
    Ghidra 12.1.4 via REA. É o alvo de todos os endereços abaixo.
  - `Plugins/Fruity/Effects/Fruity Wrapper/Fruity Wrapper_x64.dll` (SHA-256 `c600a15b…2c74`): mesmo código
    deslocado em +0x1A0 (o gravador de registros `0x36/0x37/0x35` aparece em `0x871BB5…0x871CC5` em vez de
    `0x871A15…0x871B25`). Tudo o que vale para o gerador vale para o efeito.
- **Validação de formato:** os 165 `.flp` de `FL Studio 2026/Data` + os 2 autosaves do usuário.
  Só **36 instâncias** do wrapper existem nos dados (15 arquivos: 23 em 2 demos "ZGameEditor Visualizer",
  13 em `Data/System/Render tests`). Todas são **VST2 (efeito)** das próprias Fruity *.dll de FL 11–13;
  **não há nenhum VST3 nem CLAP nos dados**, então tudo o que é específico de VST3/CLAP abaixo é só
  **[DECOMP]**. Não há VST3/CLAP instalados na máquina (`Common Files/VST3` e `/CLAP` não existem).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `W+0xNN` é um offset no objeto do wrapper (o "host" do plugin dentro da DLL, o `this` de
`FUN_00871110`); `P+0xNN` é um offset no objeto do plugin hospedado (`W+0x5050`, das classes
`TVSTPlugin` / `TVST3Plugin` / `TCLAPPlugin`). Todos os inteiros são little-endian.

---

## 1. Os eventos do FLP **[DADOS+DECOMP]**

Um plugin de terceiros aparece no FLP exatamente como um plugin nativo; quem é "de terceiros" é o
*conteúdo* do `0xD5`:

| Evento | Conteúdo para o wrapper |
|---|---|
| `0xC9` | Nome interno do plugin FL = **`"Fruity Wrapper"`** (UTF-16LE com NUL nos arquivos ≥ FL 12; ASCII + NUL nos de FL 11). É o único sinal no nível de eventos de que é um VST/VST3/CLAP/AU |
| `0xD4` | Cabeçalho "new plugin" de 52 B (13 × u32), igual ao dos plugins nativos (§1.1) |
| `0xCB` | Nome visível dado pelo usuário (opcional; nos dados aparece "Compressor", "Master Compressor"; nos render tests é o nome do VST). **Ausente** ⇒ a UI mostra o nome do registro `0x36` do blob |
| `0x80` | Cor |
| `0xD5` | **Estado do wrapper** (§2). Começa com `u32 versão` e segue com registros etiquetados |

Mesma sequência nos canais (gerador) e nos slots do mixer (efeito; no mixer o slot fecha com `0x62`,
rodada 2). Nos 36 casos a ordem foi `0xC9, 0xD4, [0xCB], [0x9B], 0x80, 0xD5` **[DADOS]**.

### 1.1 Cabeçalho `0xD4` (13 × u32) **[DADOS; significados HIP]**

Para o LYNX ele **não é necessário** para achar o plugin. Valores vistos nos 36 wrappers:

| Índice | Valores | Leitura provável |
|---|---|---|
| 0 | 0…36 (mixer) / 1 (render tests) | Nos slots do mixer bate com o **número da trilha** (o slot com `0xCB = "Master Compressor"` tem 0 = master) **[HIP forte]** |
| 1 | 0…4 | **Índice do slot** na trilha **[HIP forte]** |
| 2 | sempre 2 | ? |
| 3 | 0 | |
| 4 | 0x40 / 0x41 | **flags**; o bit 0x10 distingue gerador/efeito (ANALISE §5.1) **[DECOMP anterior]** |
| 5…8 | 0, 0/5, … | ? |
| 9…12 | p.ex. 64,176 / 208,320 / 16,128 | posição/tamanho da janela do editor **[HIP]** |

---

## 2. O blob `0xD5` do wrapper **[DECOMP+DADOS]**

Escrito por `FUN_00870c00` (o `SaveRestoreState` da DLL do wrapper) + `FUN_00871110`; lido por
`FUN_00870c00` + `FUN_00875aa0`.

### 2.1 Envelope

```
u32 versão            FL 2026 grava 12 (0x0C). Os 36 blobs dos demos têm 8.
repetir até o fim do 0xD5:
    u32 id
    u64 tamanho        (sem sinal; o leitor sempre pula para início_do_payload + tamanho)
    u8  payload[tamanho]
```

- 36/36 blobs se decompõem **exatamente** até o último byte nesse formato **[DADOS]**.
- Regras de versão no load (`FUN_00870c00`) **[DECOMP]**:
  - `versão == -1` → ignorado; `versão ≥ 13` → recusa (erro "versão mais nova").
  - `versão < 5` → formato legado de layout fixo (`FUN_008757b0`, **não analisado**; não aparece nos dados).
  - `5 ≤ versão ≤ 12` → formato de registros desta seção.
- **Codificação das strings** (registros `0x36/0x37/0x38/0x3A`): **sem terminador, sem prefixo** (o
  tamanho do registro é o tamanho em bytes). `versão ≤ 8` → **ANSI** (página de código do sistema);
  `versão ≥ 9` → **UTF-8** (o leitor/gravador passa pela conversão com página 65001, `FUN_0041b300` /
  `FUN_0041b530` → `FUN_0041b140/0041b240`) **[DECOMP]**. O gravador atual sempre usa UTF-8.
- Ids desconhecidos são **pulados** pelo leitor (ele faz *seek* para o fim do registro após cada um,
  `FUN_0074ac20`), então a lista é extensível **[DECOMP]**.
- Se o plugin não puder ser carregado, o FL **guarda o `0xD5` inteiro** e o regrava byte a byte no
  próximo save (`W+0xF6C`) **[DECOMP]**. O LYNX deve fazer o mesmo (round-trip do blob opaco).

### 2.2 Registros (ordem de gravação)

| Id | Tamanho | Tipo | Significado | Importa p/ LYNX? |
|---|---|---|---|---|
| `0x01` | 20 | struct | `i32 portaMidiEntrada` (`W+0x854`), `i32 portaMidiSaída` (`W+0x858`), `u8` opção (gravado de uma opção do wrapper; 0 é lido como 2), 11 B zero. Dados: `-1, -1, 0x0C` (35×) e `-1,-1,0x02` (1×) | não |
| `0x02` | 25 | struct | Opções do wrapper (`FUN_00874480`): `u8`, `u32`, `u32` (dados: 561, 389 ou 320/45…: provavelmente posição do editor **[HIP]**), `u32 flags` em +9 (bits de "Processing/Troubleshooting": ex. 0x400 = ponte 32/64 bits, 0x200000, 0x800000…), `u32` em +0x0D, `u8` em +0x11, `u32` em +0x12, `u16 flags2` em +0x16 | não |
| `0x1E` | 16 | 4 × u32 | `[0]` nº de entradas de áudio (pinos), `[1]` nº de saídas, `[2..3]` = 0. Dados: `1, 1, 0, 0` | opcional (layout de barramentos) |
| `0x1F` | 12·n | n × {u32 destino, u8 ligado, 3 B} | Roteamento de cada **entrada** do plugin (n = `[0]` do `0x1E`). Dados: `{0, 1}` | não |
| `0x20` | 12·n | idem | Roteamento de cada **saída** (n = `[1]` do `0x1E`; destino provavelmente deslocamento de trilha do mixer **[HIP]**) | não |
| `0x32` | 16 | u32 + 12 B zero | **Formato/tipo do plugin hospedado** (`P+0x98`), tabela §2.3 | **sim** |
| `0x33` | 4 | u32 | **VST2 `uniqueID`** (o `AEffect::uniqueID`, lido via método virtual `P+vtbl 0x2C0`). Só VST2/FL/AU/default (não VST3/CLAP/DX) | **sim (VST2)** |
| `0x39` | 4 | u32 | Só para `TVSTPlugin`: valor de `effGetVstVersion` (opcode 58) do plugin (`P+0x9C4`). Usado para converter estado VST2→VST3 | não |
| `0x34` | 16 | GUID cru | **ID de classe**: VST3 = TUID/FUID da classe; DirectX = CLSID. Para VST2 só é gravado se ≠ GUID nulo (`P+0x4B0`) | **sim (VST3)** |
| `0x3A` | var | string | **CLAP plugin id** (ex. `com.vendor.plugin`) (`TCLAPPlugin+0x9B4`). Só tipos 11/12 | **sim (CLAP)** |
| `0x36` | var | string | **Nome do plugin** (VST2: `effGetEffectName`/nome do banco; VST3: derivado do **nome do arquivo** sem extensão, não do nome da classe; CLAP/DX: nome do descritor) | sim (fallback) |
| `0x37` | var | string | **Caminho completo do binário** (`.dll`, `.vst3`, `.clap`, `.component`) na máquina que salvou (`P+0x2A6`) | sim (fallback) |
| `0x38` | var | string | Texto adicional (`P+0x3C`); só gravado se não vazio; não participa da busca do plugin. Não aparece nos dados **[HIP: fabricante ou caminho alternativo]** | não |
| `0x35` | var | bytes | **Estado do plugin hospedado**, produzido pelo método virtual `SaveRestoreState` da classe do plugin (VMT+0x1A8). Formato depende do tipo (§3) | **sim** |

Observações:
- Na versão 8 dos demos a ordem é sempre `01, 02, 1E, 1F, 20, 32, 33, 36, 37, 35` **[DADOS]**; o `0x34`
  foi omitido (GUID nulo), o `0x39` ainda não existia.
- O leitor aceita qualquer ordem; o tipo (`0x32`) e os identificadores são juntados no fim
  (`FUN_00875aa0`) **[DECOMP]**.
- O `0x37` é usado pelo FL com `/` normalizado (`FUN_0044acd0(…, '/')`) **[DECOMP; detalhe HIP]**.

### 2.3 Tipos de plugin (`u32` do registro `0x32`) **[DECOMP+STR]**

Tabela de `FUN_0074b090` (nomes da UI):

| Valor | Formato | Papel | Extensão procurada (`FUN_00871f90`) | Identificador principal |
|---|---|---|---|---|
| 0 | VST2 | efeito ("VST (fx)") | `.dll` | `0x33` uniqueID |
| 1 | DirectX | efeito | `.dll` | `0x34` CLSID |
| 2 | FL nativo | efeito | `.dll` | |
| 3 | FL nativo | gerador | `.dll` | |
| 4 | VST2 | gerador ("VSTi") | `.dll` | `0x33` uniqueID |
| 5 | DirectX | gerador | `.dll` | `0x34` CLSID |
| 7 | **VST3** | efeito | `.vst3` | `0x34` class ID |
| 8 | **VST3** | gerador/instrumento | `.vst3` | `0x34` class ID |
| 9 | AudioUnit | efeito | `.component` | |
| 10 | AudioUnit | gerador | `.component` | |
| 11 | **CLAP** | efeito | `.clap` | `0x3A` id |
| 12 | **CLAP** | gerador | `.clap` | `0x3A` id |

Nos 36 blobs dos dados: tipo 0 em todos **[DADOS]**.

### 2.4 Como o FL acha o plugin na máquina **[DECOMP]**

`FUN_00871f90` monta uma consulta `{tipo, nomeDoArquivo, nome, uniqueID, GUID, clapId}` e a manda ao
FL (dispatcher do host, opcode 7), que resolve no **banco de plugins escaneados**:

1. `nomeDoArquivo` = nome do arquivo do `0x37` (sem diretório); se vazio, o `0x36`; mais a extensão do tipo.
2. Tenta primeiro a arquitetura (32/64 bits) do binário original e depois a outra.
3. Se nada for achado e houver `uniqueID`, percorre a lista de **plugins "shell"** (VST2 com vários
   plugins num `.dll`, chave `Software\Image-Line\Shared\Plugins\Fruity wrapper\Shell plugins`) e pega o
   arquivo cujo nome bate e que contém aquele ID.
4. O FL **pode trocar VST2 ↔ VST3** do mesmo produto: tipo salvo 4 + achado 7 ⇒ vira 8; salvo 8 + achado 0
   ⇒ vira 4. Quando um estado VST2 vai para um VST3, o wrapper passa `uniqueID` e o valor do `0x39` ao
   VST3 ("There was an error setting the state of a VST2 plugin in the VST3 plugin") **[DECOMP+STR]**.
5. Falha ⇒ mensagem "Something went wrong loading the %s plugin |%s|. Please make sure it's installed
   and registered properly." e o blob é preservado (§2.1).

Ou seja, **o caminho absoluto não é a chave**; a chave é tipo + ID (uniqueID / class ID / clap id), com
o nome do arquivo e o nome como apoio.

---

## 3. O estado do plugin (registro `0x35`)

### 3.1 VST2 (`TVSTPlugin`, VMT `0x7A77C8`, slot 0x1A8 = `FUN_007ad250`) **[DECOMP+DADOS]**

Formato atual (versão −9). Nos 36 blobs ele se decompõe até o último byte **[DADOS]**.

```
i32  versão             −9 (0xFFFFFFF7). Aceita −9…−1 e ≥0 (formatos antigos, ver abaixo)
u8   flags              bit0 = "salvar todos os programas" (opção 6 do wrapper)
                        bit1 = (opção P+0x2E desligada)
                        bit2 = P+0x35
                        bit3 = o plugin declara effFlagsProgramChunks (0x20)
i32  marcadorOuTamanho  −2 ⇒ segue i64 tamanho; senão é o próprio tamanho (versões antigas gravavam 0 aqui)
[i64 tamanhoDoChunk]
se tamanhoDoChunk > 0:                         -- plugin com chunk (a maioria dos VST2 comerciais)
    i32  programaAtual
    u8   chunk[tamanhoDoChunk]                 -- resultado de effGetChunk(index = 0, isPreset = 0) → BANCO
    (fim)
senão:                                          -- plugin sem chunk: parâmetros crus
    i32  programaAtual
    i32  n                                     -- numParams, ou numParams × numPrograms se bit0
    f32  valores[n]                            -- 0…1, ordem programa-maior (prog 0 params 0..P−1, prog 1 …)
    i32  m                                     -- nº de nomes de programa
    m × char[25]                               -- nome ANSI, 24 chars + NUL, preenchido
```

Detalhes do load **[DECOMP]**:
- O chunk é aplicado com `effSetChunk` (opcode 24) **index 0 = banco**; só em arquivos versão −6 sem a
  opção "todos os programas" ele é aplicado como *preset* (index 1).
- Os floats são aplicados com `setParameter(i, v)`; se `n == numParams × numPrograms` o wrapper troca de
  programa a cada bloco. Depois, `programaAtual` é selecionado.
- A aplicação dos floats fica entre `effBeginSetProgram` (opcode 0x43 = 67) e `effEndSetProgram`
  (0x44 = 68).
- O valor mágico `123456` (0x1E240) na posição da versão significa "nada a restaurar".

Dados: nenhum dos 36 usa chunk (são Fruity VSTs de parâmetros); `n` = 1…12, `m` = 1 **[DADOS]**.

### 3.2 VST3 (`TVST3Plugin`, VMT `0x841B90`, slot 0x1A8 = `FUN_0084e5e0`) **[DECOMP]**

O `0x35` de um VST3 é **ele mesmo** um contêiner de registros, com o mesmo cabeçalho de registro
(`u32 id, u64 tamanho, payload`):

```
u32 versão = 1         (o leitor aceita 0 e 1)
registros:
  id 1: 64 B  flags; bit0 do primeiro u32 = opção P+0x2E (restaurada pelo método virtual 0x90, opção 2)
  id 3: bytes  estado do COMPONENTE  = IComponent::getState  (processador)
  id 2: bytes  estado do CONTROLADOR = IEditController::getState
  id 4: bytes  mapa de parâmetros: u32 N, depois N × i32 ParamID (−1 = slot vazio)
```

(ordem de gravação: 1, 3, 2, 4; cada um só existe se o respectivo `getState` devolveu `kResultOk`.)

Ordem de restauração **[DECOMP]** (igual à recomendação da Steinberg e à do JUCE):
1. `IComponent::setState(id 3)`;
2. se houver controlador (e a flag `P+0x808` não estiver ligada): volta o stream ao início e
   `IEditController::setComponentState(id 3)`;
3. `IEditController::setState(id 2)`;
4. reconstrói a lista de "slots" de parâmetro do FL a partir do id 4.

O mesmo trio de chamadas é refeito em `FUN_0084c6c0` a partir de cópias guardadas (`P+0x920` componente,
`P+0x918` controlador) quando o plugin é reiniciado.

### 3.3 CLAP (`TCLAPPlugin`, VMT `0x859718`, slot 0x1A8 = `FUN_0085f900`)

**Não decodificado.** Identificado apenas o método. **[HIP]** contém o `clap_plugin_state.save` cru,
possivelmente com um cabeçalho como o do VST3. Sem dados para validar.

---

## 4. Automação dos parâmetros do plugin **[DADOS+DECOMP parcial]**

- `controlID` (rodada 2 §2.7) de parâmetro de plugin:
  - **canal**: `(canal << 16) | (0x8000 + k)` — 323 alvos `0xE3` nos demos têm essa forma
    (ex. `0x00188034` = canal 0x18, parâmetro 0x34) **[DADOS]**;
  - **mixer**: `0x70000000 + (trilha << 22) + (slot << 16) + 0x8000 + k` (192 alvos) **[DADOS]**;
    arquivos < 24.2.99 usam a classe antiga `0x2…` (rodada 2).
- O que é `k` para o wrapper:
  - **VST2**: índice do parâmetro do VST (`setParameter(k, …)`) **[HIP forte]**;
  - **VST3**: índice de **slot** do FL; o `ParamID` real é `mapa[k]` do registro 4 do §3.2 **[DECOMP para o
    mapa; HIP para a ligação slot = k]**;
  - CLAP: não verificado.
- Valor: normalizado pelo setter universal (0…2³⁰ → 0…1 do parâmetro) **[HIP, rodada 2]**.

---

## Recomendações para o LYNX

### Parser do `0xD5` do wrapper

```
se nome(0xC9) != "Fruity Wrapper": não é wrapper.
r = Reader(blob)
versão = r.u32()
se versão < 5 ou versão > 12: guardar blob opaco e sair
utf8 = (versão >= 9)
info = {}
enquanto r.restante >= 12:
    id = r.u32(); tam = r.u64()
    se tam > r.restante: parar (blob truncado) ; payload = r.bytes(tam)
    caso id:
      0x32: info.tipo     = u32(payload, 0)
      0x33: info.vst2Id   = u32(payload, 0)          # fourCC = bytes em ordem big-endian do u32
      0x34: info.classId  = payload[0:16]             # TUID cru
      0x3A: info.clapId   = str(payload, utf8)
      0x36: info.nome     = str(payload, utf8)
      0x37: info.caminho  = str(payload, utf8)
      0x38: info.extra    = str(payload, utf8)
      0x1E: info.nIn, info.nOut = u32(payload,0), u32(payload,4)
      0x35: info.estado   = payload
      outros: ignorar
guardar SEMPRE o blob original para regravar se o plugin não for achado.
```

`str(p, utf8)`: bytes sem NUL; UTF-8 se `utf8`, senão Windows-1252 (ANSI) como aproximação.

### Achar o plugin (JUCE `KnownPluginList` / scan próprio)

1. Formato pelo tipo: 0/4 → VST (VST2); 7/8 → VST3; 11/12 → CLAP; 1/5/9/10/2/3 → não suportado
   (DirectX/AU/FL nativo) — preservar blob.
2. **VST2**: procurar `PluginDescription` com `pluginFormatName == "VST"` e `uniqueId == vst2Id`
   (no JUCE o `uniqueId` de VST2 é o próprio `AEffect::uniqueID`). Desempate pelo nome do arquivo do
   `caminho` e pelo `nome`. Plugins shell (Waves etc.) têm o mesmo `.dll` com vários IDs: o ID decide.
3. **VST3**: comparar `classId` com o class ID das classes do `.vst3`. No JUCE, calcule
   `deprecatedUid = hash31(bytes crus do TUID)` e compare com `PluginDescription::deprecatedUid`
   (o JUCE usa `valor = valor·31 + byte` sobre os 16 bytes, `juce_VST3PluginFormat.cpp` linha ~180).
   **[HIP]** os 16 bytes que o FL guarda são o TUID na ordem de memória do Windows (layout COM/GUID), que é
   a mesma ordem de `uid->data()` no JUCE para Windows; se não bater, tente também a versão
   "normalizada" (`uniqueId`). Desempate: nome do arquivo `.vst3` (o `0x36` de VST3 é o nome do arquivo,
   não o nome da classe).
4. **CLAP**: casar pelo `clapId` (o JUCE 8 não hospeda CLAP; fica para quando houver host CLAP).
5. Também aceitar a troca VST2 ↔ VST3 do mesmo produto como plano B (o FL faz), mas sem transportar o
   estado (a conversão VST2→VST3 depende do suporte do plugin).

### Restaurar o estado

- **VST2 com chunk** (`tamanhoDoChunk > 0`): montar um **FXB de chunk opaco** (`'CcnK'`, `'FBCh'`,
  versão, `fxID = vst2Id`, `fxVersion`, `numPrograms`, 128 B reservados, `i32 tamanho`, chunk; campos
  big-endian como no SDK VST2) e passar a `VSTPluginFormat::loadFromFXBFile` / `setStateInformation`.
  Depois `setCurrentProgram(programaAtual)` se fizer sentido. (O FL aplica como **banco**, index 0.)
- **VST2 sem chunk**: chamar `setParameter`/`AudioProcessorParameter::setValue` com os floats (já em
  0…1), por programa se `n == numParams × numPrograms`, e selecionar `programaAtual`.
- **VST3**: ler o sub-contêiner do §3.2 (`u32 versão` + registros) e montar o XML que o JUCE espera:
  `<VST3PluginState><IComponent>base64(id 3)</IComponent><IEditController>base64(id 2)</IEditController></VST3PluginState>`,
  convertido com `AudioProcessor::copyXmlToBinary` e entregue a `setStateInformation`. A ordem de
  aplicação do JUCE (component → setComponentState → controller) é a mesma do FL.
  Guardar o mapa do id 4 para traduzir automações (`k` → `ParamID`).
- **CLAP**: preservar blob até haver decodificação.

### Automação

Traduzir `controlID` com `p ≥ 0x8000` em "parâmetro `p − 0x8000` do plugin do canal/slot"; para VST3
passar pelo mapa do id 4. Valores normalizados 0…1.

---

## 5. Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| `FUN_00870c00` | `SaveRestoreState` do wrapper: grava `u32 12` + registros; no load despacha por versão (<5 legado, 5…12 registros, ≥13 recusa); preserva o blob se o plugin falhar | DECOMP |
| `FUN_00871110` | Gravador dos registros `01,02,1E,1F,20,32,33,39,34,3A,36,37,38,35` por tipo | DECOMP |
| `FUN_00875aa0` | Leitor dos registros; regra ANSI (≤8) / UTF-8 (≥9); troca VST2↔VST3; chamada do localizador | DECOMP |
| `FUN_0074a600/6f0/790/930/9c0/aa90/ac20` | Leitor/gravador de registros `u32 id + u64 tamanho`, strings, *seek* para o fim do registro | DECOMP |
| `FUN_0041b140/0041b240` (via `0041b300/0041b530`) | Conversão com página 65001 (UTF-8) | DECOMP |
| `FUN_0074b090` | Tabela tipo → "VST (fx)", "VSTi", "VST3 (gen)", "CLAP (fx)"… | DECOMP+STR |
| `FUN_00871f90` | Localizador: extensões por tipo, consulta ao host (opcode 7), arquitetura 32/64, lista de shell plugins | DECOMP |
| `FUN_00874480` | Monta o registro `0x02` (opções/flags do wrapper) | DECOMP |
| `FUN_00873070/00873100` | Portas MIDI in/out do registro `0x01` | DECOMP |
| `TVSTPlugin` VMT `0x7A77C8` → `FUN_007ad250` | Estado VST2 (versão −9, chunk via `effGetChunk` opcode 23 / `effSetChunk` 24, ou floats + nomes de 25 B) | DECOMP |
| `0x7AAA2C…0x7AAA62` | `P+0x9C4` = `effGetVstVersion` (opcode 58) → registro `0x39` | DECOMP |
| `TVST3Plugin` VMT `0x841B90` → `FUN_0084e5e0` | Estado VST3: sub-registros 1 (flags), 3 (componente), 2 (controlador), 4 (mapa de ParamID) | DECOMP |
| `FUN_0084c6c0` | Reaplica componente/controlador (`P+0x920/0x918`) em reinício | DECOMP |
| `TCLAPPlugin` VMT `0x859718` → `FUN_0085f900` | Estado CLAP (não lido) | STR+DECOMP (só identificação) |
| Strings "The component/controller state wasn't restored", "VST3 (gen)", "CLAP (fx)", "Shell plugins" | Contexto | STR |
| 36 blobs `0xD5` com `0xC9 = "Fruity Wrapper"` | Envelope v8, registros e tamanhos fixos (`01`=20, `02`=25, `1E`=16, `1F/20`=12, `32`=16, `33`=4), estado VST2 −9 até o último byte | DADOS |
| 323 + 192 alvos `0xE3` | `controlID` de parâmetro de plugin = `0x8000 + k` em canal e mixer | DADOS |
| JUCE 8.0.4 `juce_VST3PluginFormat.cpp` (`getHashForRange`, `VST3PluginState`), `juce_VSTPluginFormat.h` (`loadFromFXBFile`) | Lado LYNX (código aberto, não Image-Line) | referência |

### Pendências

- **Sem nenhum VST3/CLAP nos dados**: validar §3.2/§3.3 salvando um projeto com um VST3 e um CLAP
  gratuitos (ex. Surge XT, que tem os dois) e conferir: registro `0x34` (ordem de bytes do TUID frente ao
  `moduleinfo.json`), `0x3A`, sub-registros 1/2/3/4 do VST3.
- Formato legado (`versão < 5`, `FUN_008757b0`) e estado CLAP (`FUN_0085f900`).
- Significado exato do registro `0x38`, do byte de `0x01`, dos campos de `0x02` e dos destinos de `0x1F/0x20`.
- Confirmar que o `k` de automação de VST3 indexa o mapa do id 4.
