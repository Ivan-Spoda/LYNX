# Análise comportamental do FL Studio 2026: rodada 24 (REA) — espectros estáticos exatos: motor de timbre do Ogun, EQ de timbre/subs/LHP do Harmless e uníssono do Harmor

> **Natureza deste documento.** Continuação de `REA_rodada12.md` §2 (formato do Ogun), `REA_rodada21.md` §1
> (Harmless) e §2 (Ogun), `REA_rodada18.md` §1.3 e `REA_rodada23.md` §1 (Harmor). Engenharia reversa *clean-room*,
> apenas comportamental. Nenhum código nem pseudocódigo da Image-Line (nem da Intel IPP) foi copiado: o que está
> aqui são fórmulas e comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais
> servem só para rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> estão explicitadas como **inferência**.

- **Alvos:**

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave |
  |---|---|---|---|
  | Ogun (e Autogun, mesmo motor — rodada 12) | `Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= rodadas 12/21, conferido contra a instalação) | Delphi (VMT `0x8621f8`) | *note-on* `FUN_0086a8e0`, `ProcessParam` `FUN_008684e0`, carga `FUN_00864d00`; gerador de timbre (objeto `+0x230`): construtor `FUN_00842340`, dimensionamento `FUN_008424b0`, taxa `FUN_00842590`, tabela de fases `FUN_008426e0`, mapas `FUN_00842960`, normalização `FUN_008428c0`/`FUN_00842820`; voz de timbre: início `FUN_00842ee0`, espectro/decaimento `FUN_00843680` (+ `FUN_00843590`, `FUN_00843470`), primeiro quadro `FUN_00843220`, quadro seguinte `FUN_00843300`, *release* `FUN_008447d0`, render `FUN_00844940`, leitor `FUN_00842c60`/`FUN_00842de0`; amostragem dos mapas `FUN_0086e6c0`, eixo `FUN_00866da0`/`FUN_008669f0`, qualidade `FUN_00866840`, qualidade→oversampling `FUN_0086e620`; envelope (biblioteca): carga `FUN_00781bd0`, avaliação `FUN_00788a20`, coeficientes `FUN_00785940`, valor do ponto `FUN_00787170`, mapas tecla/velocidade `FUN_0078bb40`/`FUN_0078af50`; constantes `FUN_0062dec0`, `FUN_00844bc0`; despachante IPP `FUN_005bc070` |
  | Intel IPP da instalação | `Shared/dsp_ippv2_x64.dll` `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` | C (IPP) | `ippsRandUniform_Direct_32f` (exportação `0x1815b3fa0`, despachante; versão genérica `0x1815ed2e0`) |
  | Harmless | `Harmless_x64.dll` (= rodadas 10/21) | Delphi | janelas do EQ `FUN_0077b450`/`FUN_0077b2d0`, aplicação `FUN_00787b40`, LHP `FUN_00784120`, subs `FUN_00784110`, cores `FUN_00787990`/`FUN_007873c0`, `ProcessParam` `FUN_0077f630`, voz `FUN_00784a10`, constantes `FUN_00614c80`, despachante IPP `FUN_005acdb0` |
  | Harmor | `Harmor_x64.dll` `23673ad8…5aba` (= rodadas 11/18/23) | Delphi | distribuição do uníssono `FUN_00873e40`, `ProcessParam` `FUN_00879540` (casos 34–39), *note-on* `FUN_00883cd0`, atualização `FUN_008859a0`, voz `FUN_0088ab10` |

- **Ferramentas:** Ghidra 12.1.4 *headless* sobre os projetos só-leitura `/tmp/r12gh_Ogun_x64.dll` (programa
  `Ogun_x64.dll`), `/tmp/r21gh_Harmless` (`Harmless_x64.dll`) e `/tmp/r23gh` (`hm` = Harmor); projeto novo
  `/tmp/r24gh_ipp` (importação sem análise de `dsp_ippv2_x64.dll`). Scripts em `tmp/r24/scripts/` (os da rodada 23
  + `DecompRec.java`); *dumps* em `tmp/r24/out/`; modelos numéricos nossos em Python (`tmp/r24/py/ipprand.py`,
  `ogspec.py`, `oglynx.py`, `hmuni.py`, `env.py`, `og.py`) sobre `tmp/r14/blobs14.pkl`; teste de execução
  `tmp/r24/run/rnd.exe` (C nosso, compilado com Zig para Windows, que só carrega a DLL da IPP da instalação e
  chama `ippsRandUniform_Direct_32f`). Manual oficial lido via web.
- **Data:** 2026-10-13.
- **Conjunto de dados:** 20 canais Ogun em `Demo projects/**` (versões de estado v19 = 6, v18 = 14).

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/`.fst`/arquivos da instalação |
| **[EXEC]** | resultado obtido chamando a função da DLL da própria instalação (sem modificá-la) |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abreviada `…/plugins/`). As citações vieram de leitura web resumida; reconferir antes de virar texto de interface.

**Convenções.** Ogun: `FUN_0040bf50` = ln, `FUN_0040ad30` = exp, `FUN_0040c3d0` = raiz quadrada,
`FUN_0040c3e0` = arredondamento, `FUN_0040c4d0` = truncamento, `FUN_005fc620` = log₂ (ln × 1/ln 2),
`FUN_005fc730` → `FUN_0040bcc0` = SinCos (1ª saída seno, 2ª cosseno: tabela de quadrantes) **[DECOMP]**.
Harmless: `FUN_00409b50` = ln, `FUN_00409520` = cosseno, `FUN_00409610` = seno (núcleos polinomiais: o de
`FUN_00409390` começa em 1 − x²/2, o de `FUN_00409460` em x), `0x7ff4e8` = 1/ln 2 **[DECOMP]**. Nomes IPP pelos
despachantes (pares ponteiro ↔ nome): Ogun `0x8e9bb0` = `ippsRandUniform_Direct_32f`, `0x8e9ee0` =
`ippsMulC_32f_I`, `0x8e8fb0` = `ippsAddC_32f_I`, `0x8e9a30` = `ippsMul_32f_I`, `0x8ea3c8` = `ippsSub_32f_I`,
`0x8e9280` = `ippsMulC_32f`, `0x8e8d88` = `ippsMul_32f`, `0x8e90d0` = `ippsDivCRev_32f_I`, `0x8e8c70` =
`ippsExp_32f_A21`, `0x8ea3a8` = `ippsExp_32f_A11`, `0x8e9588` = `ippsSubCRev_32f_I`, `0x8e8d38` = `ippsSqr_32f`,
`0x8e97c8` = `ippsSum_32f`, `0x8ea380` = `ippsSet_32f`, `0x8ea2f8` = `ippsCopy_32f`, `0x8ea2c0` = `ippsAdd_32f_I`,
`0x8e9fd0`/`0x8ea1b8` = `ippsFFTGetSize/Init_R_32f`, `0x8e8fe8`/`0x8e9c58` = `ippsFFTInv_PackToR_32f(_I)`;
Harmless `0x7ef3c8` = `ippsMul_32f`, `0x7ef850` = `ippsMulC_32f`, `0x7f0960` = `ippsAddProductC_32f`
**[DECOMP]**. Constantes de *runtime* do Ogun: `0x8f92c8` = ln 0,01 (rodada 21), `0x8f94f0` = ln 2,
`0x8f9510` = ln 2/12, `0x8f953c` = ln 10/20 **[DECOMP `FUN_0062dec0`]**; **`0x8fc038` = 440·2^(75/12) =
33 488,07 Hz** (= 261,63 Hz × 2⁷) **[DECOMP `FUN_00844bc0`: exp(75·ln2/12)·440]**.

**Convenção do LYNX** (igual à rodada 23): o oscilador "spectral" soma `(2/π)·spectrum[h]·sen(2π(h·t +
spectrumPhase[h]))`; **A(h)** aqui é sempre a amplitude **física** do seno da harmônica; `spectrum[h] = A(h)·π/2`
reproduz o nível, `spectrum[h] = A(h)` reproduz a forma.

---

## 1. Ogun: o motor de timbre

**O que o LYNX supõe** (`src/core/FlGenerators.cpp`, `synthFromOgun`): seno/triângulo/serra escolhidos por
*fullness*/*richness*, parcial +1 oitava se fullness > 300. **Nada disso tem base no código** (ver abaixo): o Ogun
é um sintetizador **espectral por FFT inversa** cujo espectro é ruído pseudoaleatório moldado por mapas.

### 1.1 Estrutura **[DECOMP `FUN_008424b0`, `FUN_00842590`, `FUN_008426e0`, `FUN_00843300`, `FUN_00844940`, `FUN_00842c60`]**

- O gerador de timbre (um por instância, `+0x230`) cria uma FFT real de **ordem `P52 + q`** (tamanho
  `N = 2^(P52+q)`; `ippsFFTGetSize_R_32f` com *flag* 8) e usa **`M = 2^(P52−1) − 1` parciais** (bins 1…M).
  Richness 1…11 = P52 6…16 → M = 31…32 767, exatamente o "31 to 32,767" do manual (rodada 21) **[DECOMP + DOC]**.
- `q` (sobreamostragem da tabela) vem da qualidade (P54 = `+0x204`): qualidade 0/1 → q = 0, 2 → 1, 3/4 → 2; e se
  P52 < 10 com taxa < 88 200 Hz e q < 1, q = 1. O mesmo seletor fixa o **passo de quadro `hop` = 50 ms** (qualidade
  0–2) ou **25 ms** (3–4) (`+0x25c` × 0,001 × fs) **[DECOMP `FUN_00866840`, `FUN_0086e620`, `FUN_00864800`]**.
  Nos demos: P54 = 0 em 17, 1 em 3 **[DADOS]**.
- A cada `hop` amostras a voz monta o espectro (formato *Pack*: bin h = (Re, Im) nas posições 2h−1, 2h), faz a FFT
  inversa sem divisão para uma tabela de N amostras e **lê essa tabela em laço** com um reamostrador (passo
  fracionário, interpolação de ordem escolhida pela qualidade), com *crossfade* linear entre o quadro velho e o
  novo dentro do `hop` **[DECOMP]**. **Inferência (IPP: *flag* 8 = sem divisão):** o seno da parcial h na tabela
  tem amplitude **2·|X_h|**.
- **Frequência das parciais.** O passo de leitura é `razão · 33 488,07 · 2^q / fs` amostras de tabela por amostra
  (`+0x7c` × razão; razão = 2^(c/1200) com c = tom da voz em centésimos + `100·P1 + P2` + articulador de tom)
  **[DECOMP `FUN_00842590`, `FUN_00844660`, *note-on*]**. Como a tabela tem N = 2^(P52+q) amostras, o período da
  tabela é `f_tab = razão · 33 488,07 / 2^P52` e a parcial h soa em **`h · f_tab`**. O mesmo *note-on* usa
  `c/100 + 60` como número de nota, o que mostra que **c = 0 em C5** **[DECOMP]**; logo, para a nota de
  frequência f (261,63 Hz em C5):

  **`f_h = h · f · 2^(7 − P52)`** (inferência direta do código)

  isto é, **a fundamental da nota é a parcial h = 2^(P52−7)**; as parciais intermediárias são **inarmônicas**
  (frações de f), e a mais alta (h = M) fica em ≈ 64·f qualquer que seja a richness. Richness controla a
  **densidade** de parciais, não a largura de banda. Para P52 = 6 só existem as harmônicas pares (2f, 4f…); para
  P52 = 7 as parciais são exatamente as harmônicas 1…63.
- **Limite de banda:** só são preenchidas as parciais `h < min(M, ⌊M·2^q·0,907029/passo⌋)` (0,907029 =
  40 000/44 100) **[DECOMP `FUN_00843680`]**; **inferência:** corta em ≈ 0,4535·fs (20 kHz a 44,1 kHz).

### 1.2 Fases (fixas para todos os presets) **[DECOMP `FUN_008426e0`, `FUN_00842340`; EXEC]**

- Uma tabela base é gerada **uma vez** com M ângulos `θ_h` uniformes em [0; 6,2831855) pela IPP
  (`ippsRandUniform_Direct_32f`) com **semente fixa 3**; o bin h recebe `(cos θ_h, sen θ_h)/√M` **[DECOMP]**.
  Logo, na convenção seno: **`φ_h = θ_h/2π + 1/4` ciclos** (inferência: Re = cos, Im = sen pela ordem do SinCos
  e pela FFT inversa *Pack*).
- **Fase de partida da leitura:** no primeiro quadro, a posição inicial de cada voz de uníssono é `fase_uníssono ·
  N + j₀ − 32` (mod N), onde **j₀ = índice da amostra de menor |x|²** da tabela recém-gerada (`FUN_00842e90`) e 32 =
  metade do núcleo do interpolador (`+0xc` = 64) **[DECOMP `FUN_00843220`]**. Portanto as fases efetivas são
  `φ_h + h·(j₀ − 32)/N` (mais o deslocamento de uníssono) — calculáveis a partir da tabela.

### 1.3 O gerador uniforme da IPP (necessário para fases e sementes) **[DECOMP `0x1815ed2e0`; EXEC]**

Descrição (palavras nossas) da versão genérica de `ippsRandUniform_Direct_32f(dst, n, lo, hi, &s)`:
- duas sequências de 32 bits: (a) **subtrai-com-empréstimo**: `e_k = e_{k−2} − e_{k−3} − b`, com `b` = 1 se o
  resultado anterior ficou negativo (inicial 1); se `e_k` (com sinal) for negativo, subtrai-se 18 (aritmética
  mod 2³²); valores iniciais `e_{−2} = (s·69069 + 1013904243)·69069 + 1013904243`, `e_{−1} = s·69069 + 1013904243`,
  `e_0 = s`; (b) **congruencial** `L_k = L_{k−1}·69069 + 1013904243` com `L_0 = 0x436CBAE9`;
- saída k = `float((int32)(e_k + L_k) · (hi − lo)·2⁻³² + (lo + hi)/2)`; a semente devolvida é o último `e_k`.
- **Verificação [EXEC]:** o modelo (`tmp/r24/py/ipprand.py`) reproduz a DLL da instalação com a mesma semente de
  saída e valores idênticos para n = 1/7 e várias sementes; para n = 32 767 há diferenças de **1 ulp** em 243 de
  32 767 valores (o caminho vetorizado escolhido pela CPU arredonda diferente) — irrelevante para espectros.

### 1.4 As sementes **[DECOMP `FUN_008684e0` casos 10/11, *note-on* `FUN_0086a8e0`; DOC]**

- `ProcessParam` guarda, para Seed 1 (P10) e Seed 2 (P11): **−1 se o valor é 0**, senão `|mix(v − 0x20CF)|`, onde
  `mix` é uma mistura inteira de 32 bits (estilo Jenkins: 8 rodadas de subtração/xor com deslocamentos 8, 13, 12,
  16, 5, 3, 10, 15; constantes 0x1CFC e 0x1EEF) **[DECOMP]** (modelo: `ogspec.py`, `mix`).
- Seed 1 com o byte `+0x20C` (corpo `0xE0`) ligado: semente por nota `|mix(P10 + nota − 0x1F88)|` (constante
  0x1E42) **[DECOMP]**; 1/20 demos (Chase) **[DADOS]**.
- **Seed 2 = 0 ("Random"): semente nova a cada nota** = `|mix(contador − 0x200E)|` (constante 0x1DBC), com o
  contador `+0x244` incrementado a cada nota e iniciado de um global de execução (`PTR 0x8ea240`) **[DECOMP]** —
  **não determinístico**. Manual: "Seed 2 harmonics are randomized each time a key is played"; Seed 1 "None":
  "all harmonics are activated" **[DOC `…/plugins/Ogun_synthesis.htm`]**.

### 1.5 Do estado ao "peso" B(h) de cada parcial **[DECOMP `FUN_00843680`, `FUN_00843590`, `FUN_00843470`, `FUN_00842ee0`, *note-on*]**

Valores de entrada (no *note-on*; "art k" = saída estática do articulador k, ver §1.8):
- **fullness** `f = clamp(P8/1000 + 2·art9, −1, 1)`; dentro da voz vira **`p = −sinal(f)·(10000001^|f| − 1)·0,01`**
  (|f| = 1 → |p| = 100 000) **[DECOMP: 10000001, 1, 0,01]**;
- **morph** `m = P9/1000 · art10`; **pre-decay** `P = max(0,001; P5/1000 · art6)`;
- **rotação** `ρ = (round(art11·M + 256·M)) mod M`; **deslocamento de EQ** `s = round(clamp(art12, −0,5; 0,5)·M)`.

Cálculo:
1. Para cada semente válida S: `u = RandUniform(S, M, 0, 1)` (§1.3), girado: `u'[(i + ρ) mod M] = u[i]`.
2. Forma pela fullness: `g(u) = u` (p = 0); `((1+p)^u − 1)/p` (p > 0, i.e. f < 0: valores puxados para 0 =
   espectro esparso); `((1+|p|) − (1+|p|)^(1−u))/|p|` (p < 0, i.e. f > 0: valores puxados para 1 = espectro
   cheio). Confere com o manual: "blends between … the Seeded harmonic spectrum (-100) and the full harmonic
   spectrum" **[DOC]**.
3. **`B = m·g(u'_{S2}) + (1 − m)·G₁`**, com `G₁ = g(u'_{S1})` se Seed 1 válida, `G₁ = 1` se Seed 1 = "None";
   m = 0 dispensa a Seed 2, m ≥ 1 dispensa a Seed 1; negativos → 0 **[DECOMP]**.
4. **Forced harmonics** (mapa F, §1.6): **`B ← 1 − F·(1 − B)`** (F = 0 força a parcial a 1; F = 1 não muda); um
   mapa constante igual a 0 é descartado (sem efeito) **[DECOMP `FUN_0086e6c0`, `FUN_00842960`]**.

### 1.6 Os quatro mapas por parcial (Forced, Impulse EQ, Decay EQ, Release EQ) **[DECOMP `FUN_0086e6c0`, `FUN_00866da0`, `FUN_008669f0`, `FUN_00864d00`; DADOS]**

- No estado, depois dos 13 articuladores (cada um: cabeçalho de 16 B, envelope tipo 1, 16 B, LFO tipo 2, 20 B,
  4 mapas tipo 3 = tecla, velocidade, X, Y), vêm **4 envelopes tipo 3: 13 Forced harmonics, 14 Impulse EQ,
  15 Decay EQ, 16 Release EQ** (82 envelopes por canal em 20/20 demos) **[DECOMP `FUN_00781bd0` + DADOS]**.
  Formato do envelope: `i32 tipo, i32 versão (3), i32 n`, n × (`f64 Δx, f64 y, f32 tensão, u8 modo, 2 B, u8`),
  20 B de cabeçalho **[DECOMP]**. Avaliação: **mesma biblioteca da rodada 23 §1.2** (modos 0/2/5/6/7 conferidos:
  constante 1000001 em `FUN_00785940`, mesmas fórmulas em `FUN_00788a20`; valor = y − centro `+0x18`)
  **[DECOMP]**. Modos 1 (curva dupla) e 10 (spline, coeficientes por ponto) aparecem só em Decay EQ de 3 canais
  **[DADOS]**; suas fórmulas exatas **não foram transcritas**.
- **Eixo x da parcial de índice i = h − 1:** `x = i/32767` (linear) ou, com o byte `+0x213` (corpo `0xE7`) ligado,
  **`x = log₂(h)/15`** (15 oitavas); amostrado em `x + 5,96·10⁻⁸` **[DECOMP]**. Linear em 12/20 demos, log em 8
  **[DADOS]**. Comprimento: Forced = M; os outros = min(1,5·M; 32 768); além do fim vale o último valor.
- **Valor:** y limitado a [0, 1]; Forced usa y; os três EQ usam **`E = (3^(2y) − 1)/2`** (y = 0,5 → 1 = 0 dB;
  1 → 4; 0 → 0) **[DECOMP: 2,0, ln 3]**.
- Opção `+0x226` (corpo `0xFA`): multiplica Forced, Impulse (e Decay, se ALT DECAY) por `1/√h` (0/20 demos).
- **Padrões fixos** (inteiro `+0x214 + 4k`, corpo `0xE8 + 4k`, k = 0…3 para 13…16): valor v ≥ 1 zera o mapa nas
  posições `i mod (v+1) = v` (v < 5) ou `i mod (v−2) ≠ 0` (v ≥ 5), aplicado depois da amostragem
  **[DECOMP]**; nos demos: Forced v = 3 (Clap, Snare), Forced 1 + Decay 4 (Swebass 1/2), Impulse 4 (Turbular
  Bells 2) **[DADOS]**. (Para o Forced, 0 = "forçar".)

### 1.7 Espectro inicial e decaimento por parcial **[DECOMP `FUN_00843680`, `FUN_008428c0`, `FUN_00842820`]**

- **Amplitude "pré-decaída":** `D(h) = 0,01^(1/(B(h)/P + 0,001))` — o espectro de partida é o que restaria
  depois de um tempo `P` se cada parcial caísse 40 dB num tempo proporcional a B(h). Manual: "Timbre pre-decay.
  Morphs toward a 'Metallic' EQ mapping" **[DOC]**. Com B = 1 (Seed 1 None e m = 0) D é constante.
- **Normalização RMS:** `D̂ = D · g / √(ΣD²/M)`, com `g = 1`, ou, se o byte `+0x225` (corpo `0xF9`, 11/20
  demos) estiver ligado, `g = 0,5/r`, `r = √(Σ E_imp²/M)` (com ALT DECAY: o maior entre os r de Impulse e Decay)
  **[DECOMP]**.
- **Espectro da nota no início:** `X_h = (cos θ_h, sen θ_h)/√M · D̂(h) · E_imp(h − 1 − s)` (índice limitado ao
  mapa), zerado fora de [primeira, última parcial com E_imp > 5,96·10⁻⁸] quando s = 0 e acima do limite de banda.
  Na tabela: **`A_tab(h) = 2·D̂(h)·E_imp(h−1−s)/√M`**, fase §1.2 **[DECOMP + inferência IPP]**.
- **Decaimento (ALT DECAY desligado):** a cada `hop` o espectro é multiplicado, por parcial, por
  `0,01^(1/(T·B(h)·E_dec(h−1−s) + T₀))`, com `T = max(0, t_dec·fs/hop − 0,025·fs/hop)` e `T₀ = 0,025·fs/hop`
  **[DECOMP: ln 0,01, 0,025]**; t_dec = `((21^(P6/10⁴) − 1)·997,5·art7 + 50)` ms (rodada 21). Logo cada parcial cai
  **40 dB em `τ_h = (t_dec − 0,025)·B(h)·E_dec(h) + 0,025` s**; P6 = 10 000 → sem queda (rodada 21) **[DECOMP]**.
  Confirma o manual: "Combines with the Decay mapping … that defines per-harmonic decay time" **[DOC]**.
- **Release:** a mesma lei com Release EQ e t_rel: `τ_h,rel = (t_rel − 0,025)·B(h)·E_rel(h) + 0,025` s
  **[DECOMP `FUN_008447d0`]**.
- **ALT DECAY** (byte `+0x224`, corpo `0xF8`; 6/20 demos): o espectro passa do formato do Impulse EQ ao formato
  do Decay EQ ("sets an explicit decay spectrum" **[DOC]**), com coeficiente por quadro `1 − 0,01^(1/T)`
  **[DECOMP]**; a mistura exata quadro a quadro **não foi transcrita**.

### 1.8 Articuladores tímbricos no caso estático **[DECOMP `FUN_0078bb40`, `FUN_0078af50`, `FUN_00787170`, `FUN_0086d9e0`; DADOS]**

O valor estático de um articulador k ≥ 5 combina o mapa de tecla (avaliado em nota/120) e o de velocidade pelo modo
do articulador (`+8`: 0 produto, 1 soma, outro 2·produto), e depois as tabelas X/Y (513 pontos) se os mapas X/Y não
forem neutros; cada mapa devolve **y − centro**. Nos demos, os mapas padrão valem 0,5 nos articuladores 9, 11, 12
(e 1, 2, 3) e 1,0 nos 6, 7, 8, 10 **[DADOS]**; **inferência:** centro 0,5 nos primeiros → art9 = art11 = art12 = 0,
art6 = art7 = art10 = 1 com mapas padrão. O campo de modo `+8` **não foi localizado no estado** (não confirmado);
os modelos abaixo usam esses valores neutros.

### 1.9 Verificação nos demos **[DADOS + modelo `tmp/r24/py/ogspec.py`, `oglynx.py`]**

- **Determinísticos: 9/20** (Seed 2 válida ou m = 0): Metal Drums Drum, Tibetan, Bellish 11, High Chords, Kick,
  Clap, Snare, Trumpet, Turbular Bells 2. **11/20 usam Seed 2 = Random com m > 0** (espectro novo a cada nota).
- **Harmônicas da nota representáveis no LYNX** = parciais h = h'·2^(P52−7) (P52 ≥ 7) ou h' par (P52 = 6). Fração
  da energia do espectro inicial que cai nessas harmônicas: Bellish 11 e Trumpet (P52 = 7) **100 %**; Kick (P52 = 9)
  22,6 %; Clap 6,9 %; High Chords 5,3 %; Snare 1,5 %; Tibetan 0,4 %; Metal Drums 0,2 %; Turbular Bells ≈ 0.
- Exemplos (A = amplitude na tabela, antes de volume/uníssono; h' = harmônica da nota; φ em ciclos sem o
  deslocamento j₀ de §1.2; τ = −40 dB):

| Canal | P52 / M | B | A(h' = 1…6) | φ(1…4) | τ(1…4) s |
|---|---|---|---|---|---|
| Trumpet (Electric Loop) | 7 / 63 | 1 (sem sementes) | 0,9087; 0,8610; 0,8151; 0,7711; 0,7290; 0,6888 | 0,919; 0,380; 0,130; 0,985 | 0,083; 0,080; 0,076; 0,073 |
| Bellish 11 (Edlan & Ella Noël) | 7 / 63 | Seed 2 = 29, m = 1, f = −1 | 0; 0; 0; **0,5932**; 0; 0 | idem | 0,026; 3,41; 0,025; 2,06 |
| Kick (Electric Loop) | 9 / 255 | 1 | 0,2661; 0,1533; 0,2726; 0,3955; 0,0759; 0,0712 | 0,985; 0,483; 0,779; 0,508 | 0,100 (todas) |
| Clap (Electric Loop) | 12 / 2047 | Seed 1 = 68, f = −1, Forced v = 3 | 0,0330 (todas) | 0,144; 0,378; 0,285; 0,774 | 0,490 |

  As fases 0,919/0,380/0,130/0,985… são as das parciais 1, 2, 3, 4 da tabela de semente 3 e se repetem em todos os
  presets com P52 = 7 **[EXEC + DECOMP]**.

---

## 2. Harmless: EQ de timbre, subs e LHP

**O que o LYNX supõe** (`src/core/FlSynths.cpp`, `synthFromHarmless`): cor + máscara no oscilador "spectral";
sem EQ de timbre e sem LHP; Sub 1 como seno à parte.

### 2.1 Janelas do EQ de 6 bandas **[DECOMP `FUN_0077b450`, `FUN_0077b2d0`; DOC]**

- Calculadas **uma vez** (tabelas globais) sobre a tabela de razões de base `r = {1; 0,5; 1,5; 2,5; 1; 2; 3; …; 524}`
  (528 posições). Para a banda k = 0…5 e a posição de razão r: `x = log₂(r) − k`; **`W_k = (cos(π·clamp(x, −1, 1)) +
  1)/2`** (janela de Hann de ±1 oitava); **banda 0 vale 1 para x < 0** (prateleira baixa) e **banda 5 vale 1 para
  x > 0** (prateleira alta) **[DECOMP: centros 0…5, largura 1, π, 0,5]**. Centros nas harmônicas **1, 2, 4, 8, 16,
  32**. As 6 janelas somam 1 em toda posição (**inferência** numérica). Manual: "a floating equalizer, relative to
  the root note" **[DOC `…/plugins/Harmless.htm`]**.
- Como são calculadas sobre as razões de base, **não acompanham o *detune* p39** (inferência: tabelas globais
  construídas uma vez).

| h (ou posição) | W₀ | W₁ | W₂ | W₃ | W₄ | W₅ |
|---|---|---|---|---|---|---|
| Sub 1 (0,5) e h = 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| Sub 3 (1,5) | 0,3681 | 0,6319 | 0 | 0 | 0 | 0 |
| h = 2 | 0 | 1 | 0 | 0 | 0 | 0 |
| Sub 4 (2,5) | 0 | 0,7654 | 0,2346 | 0 | 0 | 0 |
| h = 3 | 0 | 0,3681 | 0,6319 | 0 | 0 | 0 |
| h = 5 / 6 / 7 | 0 | 0 | 0,7654 / 0,3681 / 0,0888 | 0,2346 / 0,6319 / 0,9112 | 0 | 0 |
| h = 12 / 24 | 0 | 0 | 0 | 0,3681 / 0 | 0,6319 / 0,3681 | 0 / 0,6319 |
| h ≥ 32 | 0 | 0 | 0 | 0 | 0 | 1 |

### 2.2 Como os ganhos entram **[DECOMP `FUN_00787b40`, `ProcessParam` casos 5–10]**

- Ganho da banda k (parâmetro p(5+k), ±1024): **`g_k = (3^((v+1024)/1024) − 1)/2`** (v = 0 → 1; −1024 → 0;
  +1024 → 4) **[DECOMP]**.
- **`A_EQ(pos) = A_cor(pos) · Σ_k g_k·W_k(pos)`** (multiplicação pela janela e soma ponderada por banda) **[DECOMP]**.
- Ordem na voz: tabela da cor × EQ → máscara (rodada 21 §1.2) → LHP (§2.4) → ganhos das 4 posições especiais
  (subs, §2.3) **[DECOMP `FUN_00784a10`]**; acima da posição 464 (harmônica 461) uma rampa
  `(cos((i−1)·π/65) + 1)/2` atenua as últimas 64 posições (irrelevante para 256 harmônicas).
- **Dados:** EQ ≠ 0 em **96/139** canais Harmless; o valor mais comum ≠ 0 é −1024 (banda desligada) **[DADOS]**.

### 2.3 Sub 1 / Sub 3 / Sub 4 **[DECOMP `FUN_00787990`, `FUN_00784110`, `FUN_00787ce0`; DOC]**

- Posições 1/2/3 da tabela, razões 0,5 / (1 + 0,5D) / (1 + 1,5D), D = 2^(p39/1200) (rodada 21).
- Nível: **`Sub1 = (p2/1024)·A_cor(1)·g₀`**, **`Sub3 = (p3/1024)·A_cor(1,5)·(0,3681·g₀ + 0,6319·g₁)`**,
  **`Sub4 = (p4/1024)·A_cor(2,5)·(0,7654·g₁ + 0,2346·g₂)`** — a cor é avaliada no índice fracionário (i = 0,5 e
  1,5) pela mesma lei da cor (ex.: Saw → 1/1,5 e 1/2,5) **[DECOMP]**; a regra de paridade das cores pares/ímpares
  em índice fracionário **não foi conferida**. A máscara não age sobre as subs **[DECOMP]**. Fases: as da cor nos
  mesmos índices.
- Manual: "Sub 3 is the 3rd harmonic", "Sub 4 is the 4th harmonic" (da Sub 1) **[DOC]** — coerente com 1,5f e 2,5f.
- **Dados:** p3 > 0 em 14/139, p4 > 0 em 12/139 (rodada 21) **[DADOS]**.

### 2.4 Low Harmonics Protection exata **[DECOMP `FUN_0077b450`, `FUN_00784120`, `ProcessParam` caso 1; DOC]**

- `w = (11^(p1/1024) − 1)/10` (p1 = 1024 → 1) **[DECOMP]**.
- Para as 8 primeiras posições: **`A = A_masc + (A_EQ − A_masc)·p_k·w`**, isto é, por harmônica n = 1…4:
  **`A(n) = A_cor(n)·E(n)·[m_n + (1 − m_n)·p_n·w]`**, com `m_n` o fator da máscara (rodada 21) e
  `p = ((cos(π·log₂(max(r,1))/log₂5) + 1)/2)^5` **[DECOMP]**: **p = 1 (n = 1); 0,0831 (n = 2); 0,000625 (n = 3);
  ≈ 2·10⁻⁷ (n = 4)**; posições especiais: 1; 1; 0,4474; 0,00925.
- Efeito: a LHP **desfaz a máscara** (não o EQ) nas primeiras harmônicas — confere com o manual: "Prevents
  filtering functions (harmonic mask) from removing the first few harmonics" **[DOC]**.
- **Dados:** p1 = 256 (padrão; w = 0,0817) em 99/139; 0 em 20 **[DADOS]**.

---

## 3. Harmor: uníssono

### 3.1 Parâmetros **[DECOMP `FUN_00879540` casos 34–39; DFM]**

| Tag | Controle (DFM) | Campo da parte | Lei |
|---|---|---|---|
| 34 | *Unison order* 1…9 | `+0x1BC` = n | também `1/n`, `1/(n−1)`, `0,5/(n−0,5)` e ganho `+0x1C8` |
| 35 | *Unison distribution* (combo: Classic, Blurred, Uniform, Random, —Special, Hz) | `+0x1B8` = 0…4 | §3.2 |
| 36 | *Unison alternate distribution* | (lido na nota) | **inverte a polaridade** (sinal do ganho) das vozes com (k + parte) ímpar **[DECOMP]**; manual: "Alternates gain polarity" **[DOC `…/plugins/Harmor.htm`]** |
| 37 | *Unison panning* 0…256 (padrão 192) | `+0x1D8` | `v/192` (v ≤ 192) ou `1 + (v−192)·0,046875` (até 4) |
| 38 | *Unison pitch thickness* 0…256 | `+0x1C0` | `(41^(v/256) − 1)·1,875` centésimos (rodada 18) |
| 39 | *Unison phase* −1000…1000 (padrão 500) | `+0x1C4` | `v·0,001` |

### 3.2 Deslocamento de tom por voz `d_k` (k = 0…n−1) **[DECOMP `FUN_00873e40`, tabela `0x928290`]**

- Base: `|2k/(n−1) − 1|` (tipos 0–2), `|2(k + 0,25)/(n − 0,5) − 1|` (tipo 3), `|2(k + 0,5)/n − 1|` (tipo 4); **sinal
  negativo para k ímpar, invertido para as vozes da metade superior quando n é ímpar** (2k > n).
- Tipo 0 **Classic**: base com sinal **+ (1/(n−1))·J0[k]**, J0 = {0,306931; −0,28972; 0,392405; −0,223022;
  0,508197; −0,236641; 0,43662; −0,348315; 0,274336}; depois **subtrai a média**.
- Tipo 1 **Blurred**: base com sinal **× J1[k]**, J1 = {0,890472; 1,076426; 0,911577; 1,102536; 1,034126;
  1,166861; 0,853971; 1,206273; 1,0}; depois subtrai a média.
- Tipo 2 **Uniform**: base com sinal, sem centragem.
- Tipo 3 **Random**: a base do tipo 3 **+ ruído uniforme ±0,5/(n−0,5) sorteado a cada nota** (`ippsRandUniform`
  com semente de execução) — não determinístico **[DECOMP `FUN_00883cd0`]**.
- Tipo 4 **Hz**: o deslocamento por voz é forçado a 1 e uma tabela aleatória de 520 valores é sorteada na nota;
  o uso dessa tabela **não foi seguido** (não confirmado).
- **Desafinação da voz k** (centésimos) = **`d_k · s₃₈ · (articulador unison pitch)²`** somada ao tom da voz
  (rodada 18) **[DECOMP `FUN_008859a0`, `FUN_00883cd0`]**; além disso a voz passa `d_k·s₃₈` à amostragem da curva
  *Harmonic unison pitch* (envelope de *shaping* 8, rodada 23) — **inferência:** escala por harmônica, 1 com a
  curva padrão.

Tabela `d_k` (modelo `tmp/r24/py/hmuni.py`):

| n | Classic (0) | Blurred (1) | Uniform (2) |
|---|---|---|---|
| 2 | +1,2983 −1,2983 | +0,9834 −0,9834 | +1 −1 |
| 3 | +1,0852 −0,2131 −0,8721 | +0,8975 +0,0070 −0,9045 | +1 0 −1 |
| 4 | +1,0868 −0,4455 +0,4486 −1,0899 | +0,9572 −0,2921 +0,3706 −1,0358 | +1 −⅓ +⅓ −1 |
| 5 | +1,0420 −0,6072 +0,0634 +0,4095 −0,9077 | +0,9166 −0,5121 +0,0261 +0,5774 −1,0080 | +1 −½ 0 +½ −1 |
| 9 | +1,0270 −0,7976 +0,5377 −0,2893 +0,0521 +0,2090 −0,4568 +0,6951 −0,9771 | +0,8868 −0,8110 +0,4522 −0,2793 −0,0036 +0,2881 −0,4306 +0,9011 −1,0036 | +1 −¾ +½ −¼ 0 +¼ −½ +¾ −1 |

(n = 6–8 e tipos 3/4 em `tmp/r24/out/hm_unison_offsets.txt`, gerado por `hmuni.py`.)

### 3.3 Pan, ganho e fase por voz **[DECOMP `FUN_00883cd0`, `FUN_0088ab10`, `ProcessParam` caso 34]**

- **Pan da voz k:** `clamp((2k/(n−1) − 1)·pan₃₇, −1, 1)` — **linear na ordem das vozes** (não alterna como o tom).
- **Ganho de cada voz:** **`1 − (3^(1 − 1/n) − 1)/2`** (n = 2 → 0,634; 3 → 0,460; 4 → 0,360; 5 → 0,296; 9 → 0,172);
  n = 1 → 1 **[DECOMP: 1, 3, 0,5]**.
- **Fase de partida:** a voz k recebe `+(k/n)·(p39/1000)·(articulador unison phase)` somado à fase base; com o
  produto = 1 entra num ramo especial (fase livre). Unidade (provavelmente ciclos) **não confirmada**.
- **Dados (198 partes):** n ≥ 2 em 78; distribuição **Blurred em 58**, Classic 8, Uniform 4, Random 3, Hz 5; p36 = 0
  em todas; pan mais comum 181/256/192 **[DADOS]**.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Ogun: motor | síntese por FFT inversa de N = 2^(P52+q); **M = 2^(P52−1) − 1 parciais**, quadros de 50/25 ms **[DECOMP]** |
| 1b | Ogun: frequência | **f_h = h·f·2^(7−P52)** (fundamental da nota = parcial 2^(P52−7)) **[DECOMP + inferência]** |
| 1c | Ogun: fases | aleatórias fixas: IPP RandUniform semente 3, `φ_h = θ_h/2π + ¼`; gerador da IPP descrito e reproduzido **[DECOMP + EXEC]** |
| 1d | Ogun: sementes/fullness/morph | `B = m·g(u₂) + (1−m)·g(u₁)`; g pela fullness com `|p| = (10000001^|f| − 1)/100`; Seed 2 = 0 → aleatória por nota **[DECOMP + DOC]** |
| 1e | Ogun: espectro inicial | `A = 2·D̂·E_imp/√M`, `D = 0,01^(1/(B/P + 0,001))`, normalização RMS **[DECOMP]** |
| 1f | Ogun: mapas | Forced `1 − F(1−B)`; EQ `(3^(2y) − 1)/2`; eixo `i/32767` ou `log₂h/15`; padrões fixos **[DECOMP]** |
| 1g | Ogun: decaimento | **τ_h = (t − 0,025)·B·E_dec + 0,025 s** a −40 dB (idem release) **[DECOMP]** |
| 1h | Ogun: demos | 9/20 determinísticos; só P52 = 7 (2 canais) cabe 100 % em harmônicas **[DADOS]** |
| 2a | Harmless: EQ | Hann de ±1 oitava em log₂(r), centros h = 1, 2, 4, 8, 16, 32, prateleiras nas pontas; `A·Σ g_k W_k` **[DECOMP]** |
| 2b | Harmless: subs | níveis com a cor em 1,5/2,5 e as janelas do EQ; freq. 0,5f, (1+0,5D)f, (1+1,5D)f **[DECOMP]** |
| 2c | Harmless: LHP | desfaz a máscara: `m + (1−m)·p·w`, p = 1; 0,083; 0,0006; ~0 (h = 1…4) **[DECOMP + DOC]** |
| 3a | Harmor: deslocamentos | tabela §3.2 (Classic com jitter J0, Blurred × J1, Uniform), centrados (0/1) **[DECOMP]** |
| 3b | Harmor: pan/ganho/fase | pan linear `(2k/(n−1) − 1)·pan`; ganho `1 − (3^(1−1/n) − 1)/2`; fase `k/n·p39/1000` **[DECOMP]**; p36 = polaridade alternada **[DECOMP + DOC]** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Ogun (`synthFromOgun`):** abandonar a escolha seno/triângulo/serra. Para **P52 = 7** (e P52 = 6, só
   harmônicas pares) e **canal determinístico** (Seed 2 ≠ 0 ou P9 = 0), gerar o oscilador "spectral" com
   `A(h') = 2·D̂(h)·E_imp(h−1−s)/√M`, `h = h'·2^(P52−7)`, `spectrumPhase = θ_h/2π + 0,25` (θ pela reimplementação do
   gerador da §1.3, semente 3, M valores em [0; 6,2831855)), seguindo §1.4–§1.7 (mapas 13–16 com o eixo e os
   padrões da §1.6, avaliação de envelope da rodada 23). Para P52 > 7 o espectro é majoritariamente inarmônico:
   o espectral só captura as parciais múltiplas de 2^(P52−7) (registrar no log a fração de energia perdida, §1.9);
   para Seed 2 aleatória, registrar no log que o timbre muda a cada nota.
   - Decaimento: `τ_h` da §1.7 é por harmônica; se o LYNX só tem um envelope, a média ponderada por A² é uma
     **inferência** (não do FL) — manter o envelope atual e registrar.
2. **Harmless (`synthFromHarmless`):** multiplicar cada harmônica por **`E(h) = Σ g_k·W_k(h)`** (§2.1/§2.2, g da
   §2.2); aplicar a **LHP** às harmônicas 1–4 após a máscara (`m + (1 − m)·p·w`); níveis das subs pela §2.3.
   Sub 3/Sub 4 (1,5f/2,5f) não cabem na série da nota; **opção** (inferência de representação, não do FL): tocar o
   espectral uma oitava abaixo com `spectrum[2n] = A(n)`, `spectrum[1] = Sub1`, `[3] = Sub3`, `[5] = Sub4`
   (válido só com p39 = 0, até 128 harmônicas).
3. **Harmor (`synthFromHarmor`, bloco do uníssono):** trocar `±spread` por `d_k·s₃₈` da tabela §3.2 (tipo p35;
   Blurred é o caso dominante) usando as vozes com os maiores |d_k| e o centro quando houver 3 osciladores;
   pan `(2k/(n−1) − 1)·pan₃₇` (com `pan₃₇` pela lei da §3.1, não `p37/256`), ganho por voz
   `1 − (3^(1−1/n) − 1)/2`; p35 = 3/4 → registrar no log (aleatório).

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| Ogun `FUN_008424b0` (ordem P52+q, `(2^(ordem−1) >> q) − 1`), `FUN_00617260` (FFT R 32f, flag 8) | tamanho da FFT e nº de parciais | DECOMP |
| Ogun `FUN_00842590` (`0x8fc038`·2^q/fs), `FUN_00844bc0` (440, 75, ln2/12), `FUN_00844660`, `FUN_00842c60`, `FUN_00611750`, *note-on* (c/100 + 60, 1/1200, ln 2) | frequência das parciais | DECOMP |
| Ogun `FUN_008426e0` (RandUniform 0…6,2831855, semente 3, 1/√M, SinCos) | fases fixas | DECOMP |
| IPP `0x1815ed2e0` (69069, 0x3C6EF373, 0x436CBAE9, 18, 2⁻³², 0,5) + `tmp/r24/run/rnd.exe` | gerador uniforme | DECOMP + EXEC |
| Ogun `FUN_008684e0` casos 5–11 (0,001; 1e−4; mistura 0x20CF/0x1CFC/0x1EEF) e *note-on* (0x1F88/0x1E42, 0x200E/0x1DBC, contador `+0x244`) | sementes | DECOMP |
| Ogun `FUN_00843680` (+ `0x844618` 0,025; `0x844630` 0,001; `0x84463c` 0,001; `0x844640` 0,90703; ln 0,01), `FUN_00843590`, `FUN_00843470` (10000001, 0,01) , `FUN_00842ee0` | B, D, normalização, decaimento, limite de banda | DECOMP |
| Ogun `FUN_00842960`, `FUN_008428c0`/`FUN_00842820` (0,5) | mapas, janela de parciais, normalização opcional | DECOMP |
| Ogun `FUN_0086e6c0` (0, 1, 2, ln 3, 0,5, padrões), `FUN_00866da0` (32767; log₂, 15), `FUN_008669f0` (1,5M, 1/√h) | amostragem dos mapas | DECOMP |
| Ogun `FUN_00864d00`, `FUN_00781bd0`, `FUN_00785940` (1000001), `FUN_00788a20`, `FUN_00787170` | formato e avaliação dos envelopes | DECOMP |
| Ogun `FUN_00866840`, `FUN_0086e620` | qualidade → q, hop | DECOMP |
| Ogun `FUN_00843220`, `FUN_00842e90` | posição inicial de leitura (mín. |x|²) | DECOMP |
| 20 canais Ogun (`og.py`, `env.py`, `ogspec.py`, `oglynx.py`) | flags, 82 envelopes, espectros de exemplo | DADOS |
| Manual Ogun | richness, fullness, sementes, pre-decay, mapas, ALT DECAY | DOC `…/plugins/Ogun_synthesis.htm` |
| Harmless `FUN_0077b450`/`FUN_0077b2d0` (0…5, 1, −1, π, 0,5; log₅; ^5), `FUN_00787b40`, `FUN_00784120`, `FUN_00784110`, `FUN_00787990` (0,5/1,5), `FUN_0077f630` casos 1 e 5–10, `FUN_00614c80` | EQ, LHP, subs | DECOMP |
| 139 canais Harmless (`tmp/r21/py/hl.py`) | uso de EQ/LHP/subs | DADOS |
| Manual Harmless | EQ flutuante, LHP, subs | DOC `…/plugins/Harmless.htm` |
| Harmor `FUN_00873e40` (tabela `0x928290`; 2, 1, 0,25, 0,5), `FUN_00879540` casos 34–39 (1/n, 3, 0,5, 1/192, 0,046875, 41, 1,875, 0,001), `FUN_00883cd0`, `FUN_008859a0`, `FUN_0088ab10` | uníssono | DECOMP |
| DFM Harmor (`FatOrderSelect`, `FatTypeSelect`, `FatInvBox`, `FatPanWheel`, `FatPitchWheel`, `FatPhaseWheel`) | nomes e faixas | DADOS (DFM) |
| 198 partes Harmor (`tmp/r23/py/hm.py`) | distribuição dos tipos | DADOS |
| Manual Harmor | ordem, espessura, pan, fase, polaridade alternada | DOC `…/plugins/Harmor.htm` |

### Questões em aberto

- Ogun: campo de modo `+8` dos articuladores (combinação tecla/velocidade/X/Y) e centros por articulador
  (o 0,5 dos bipolares é inferência pelos dados); fórmulas dos modos de envelope 1 e 10; mistura quadro a quadro do
  ALT DECAY; papel exato do interpolador (ordem por qualidade) e do *crossfade* entre quadros; valor inicial do
  contador de Seed 2 (global de execução); escala absoluta de saída (volume mestre e uníssono da rodada 21 sobre a
  tabela); confirmação auditiva de `f_h = h·f·2^(7−P52)`.
- Harmless: paridade das cores 3–10 em índice fracionário (subs 3/4); confirmação de que as janelas não acompanham
  p39.
- Harmor: uso da tabela aleatória do tipo Hz; unidade da fase de uníssono e o ramo "produto = 1"; papel exato da
  curva *Harmonic unison pitch* sobre `d_k·s₃₈`; o manual lista as distribuições numa ordem diferente da do combo do
  DFM (a ordem do código é a do DFM).
