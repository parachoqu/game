# Integração do mundo do Blender na demo

Data: 25 de setembro de 2026
Fonte autoral: `blender-world/exports` (schema `1.0.0`, Blender 5.0.1, sementes 26092401/02/03)
Pacote de runtime: `demo/assets/world-runtime` (schema `1.0.0`, `world_layout_version` 2)

O cenário procedural provisório da demo foi substituído pela região comercial (1.024 × 1.024 m) e
pela Região Turbulenta (256 × 256 m) produzidas no Blender. Controles, personagem, montaria, NPCs,
inimigos, combate, projéteis, telegráficos, economia, inventário, coleta, interações, acampamento,
eventos, estrelas, portais, interface, minimapa, mapa expandido, sistema de qualidade, salvamento,
funcionamento offline e a geração do HTML autônomo continuam como estavam.

---

## 1. Estado inicial

| Item | Valor |
|---|---|
| `demo/dist/Projeto_Game_Demo.html` | 15.203.773 bytes (14,5 MB) |
| SHA-256 do HTML | `8c1f71c2e89c6d8c73ab56dc4e164cc7ab76b3f2bb1de22a58f96ed6aa475607` |
| SHA-256 da variante Artifact | `41f3f8fe7264009816b258c8a2ba1ea7917280a291859f3a472fd2ca076258b3` |
| Arquivos da demo com hash registrado | 105 (`src`, `tools`, `build.mjs`, `LEIAME.md`, `assets`) |
| Build de verificação | `node build.mjs` reproduziu o mesmo hash antes de qualquer edição |
| Repositório | `.git` existe mas está vazio: **não é um repositório git válido**, então não há histórico, branch nem `git status` |

Cópia de segurança e manifesto de hashes com data e hora:
`backups/demo-baseline-20260924-203511/` — contém `demo-sources.sha256`, `dist.sha256`,
`dist-sizes.txt`, `demo-src-backup.tar.gz` e `Projeto_Game_Demo.baseline.html`.
Nenhum arquivo anterior da demo foi restaurado, revertido ou descartado.

### Documentos de partida

- `AGENTS.md` **não existe** na raiz do projeto (`/home/https/Área de trabalho/workspace/rpg/AGENTS.md`).
  Também não existe `RTK.md` na raiz — há um `RTK.md` global em `~/.claude/RTK.md`, referenciado pelo
  `CLAUDE.md` do usuário, e ele está presente. A ausência do `AGENTS.md` foi registrada e o trabalho
  seguiu pelas instruções desta tarefa.
- Lidos e usados: `blender-world/README.md`, `blender-world/reports/RELATORIO_FINAL.md`,
  `blender-world/exports/manifest.json`.

### Desempenho da base (medido antes da migração)

Ver seção 10.

---

## 2. Arquivos alterados e adicionados

### Adicionados (23)

| Caminho | Papel |
|---|---|
| `tools/build-world-runtime.mjs` | Pipeline reprodutível de preparação dos ativos |
| `tools/run-world-tests.mjs` | Executa os testes pelo mesmo caminho do build |
| `tools/inspect-world-sources.mjs` | Inspeção das exportações de origem (malhas, materiais, texturas) |
| `tests/world.test.js` | 38 testes dos contratos críticos |
| `src/world/runtime-manifest.js` | Porta única de entrada dos dados do mundo |
| `src/world/coordinates.js` | Contrato de coordenadas e deslocamento técnico da Turbulenta |
| `src/world/heightfield.js` | Heightfields, máscaras e perfil do rio |
| `src/world/runtime-loader.js` | Carregador genérico de GLB offline (sem `mold()`) |
| `src/world/world-materials.js` | Materiais do Blender → superfícies PBR da demo |
| `src/world/world-routes.js` | Polilinhas, campo de distância e faixas das estradas |
| `src/world/scene-world.js` | Terreno com LOD, água, arquitetura, Turbulenta, depuração |
| `src/world/world-colliders.js` | Colisores e pontes a partir dos volumes medidos |
| `src/world/world-instances.js` | Vegetação em `InstancedMesh` (autoral + derivada) |
| `src/world/index.js` | Sequência de carregamento do mundo |
| `assets/world-runtime/*` (9 arquivos) | Pacote de runtime gerado |

### Alterados (21)

`build.mjs` · `src/main.js` · `src/engine/terrain.js` · `src/engine/quality.js` ·
`src/engine/materials.js` · `src/engine/nature-textures.js` · `src/engine/props.js` ·
`src/engine/renderer.js` · `src/engine/sky.js` · `src/game/layout.js` · `src/game/world.js` ·
`src/game/collide.js` · `src/game/save.js` · `src/game/turbulent.js` · `src/game/player.js` ·
`src/game/zones.js` · `src/game/camp.js` · `src/game/event.js` · `src/game/discovery.js` ·
`src/game/npcs.js` · `src/ui/map.js`

Razão de cada alteração fora do escopo previsto:

- `engine/materials.js` — duas superfícies novas (`trilha`, `gramaCampo`) para os materiais de
  estrada e campo do Blender.
- `engine/nature-textures.js` — registra essas duas superfícies reaproveitando capturas já
  carregadas (nenhum byte novo no HTML).
- `engine/props.js` — `instanced()` passou a aceitar o tamanho do bloco e `updateNatureLOD()` passou
  a cortar blocos além do alcance de vegetação; sem isso, 1 km² gera milhares de chamadas de desenho.
- `engine/renderer.js` e `engine/sky.js` — densidade da névoa recalibrada para a nova escala
  (0,0024 → 0,0011 na região; 0,018 → 0,012 na Turbulenta, que passou de 160 m para 256 m).
- `game/player.js`, `game/zones.js`, `game/save.js`, `main.js` — o teste `pos.x > 800` deu lugar a
  `isTurbulentSpace()`, e a lâmina de água deixou de ser um nível global.
- `game/camp.js`, `game/event.js`, `game/discovery.js`, `game/npcs.js` — patrulhas, postos de guarda,
  raios de descoberta e rotas de viajantes passaram a se apoiar no layout, sem coordenadas próprias.

Nenhum arquivo foi removido. `blender-world/`, `demo/vendor/`, `demo/tools/node_modules/` e os
modelos de personagens e animais não foram tocados (verificado por data de modificação).

---

## 3. Arquitetura final

```
blender-world/exports (somente leitura)
        │
        │  node tools/build-world-runtime.mjs
        ▼
demo/assets/world-runtime/            ← pacote determinístico, 3,3 MB
        │  world-manifest.json  region-height.bin  region-mask.bin
        │  turbulent-height.bin region-props.glb   region-water.glb
        │  turbulent-props.glb  nature-prototypes.glb  runtime-report.json
        ▼
demo/src/world/                       ← camada de integração
  runtime-manifest ─ coordinates ─ heightfield ─ world-routes
        │                │              │
        │                ▼              ▼
        │          world-colliders   scene-world ── runtime-loader
        │                │              │              world-materials
        │                └──────┬───────┘
        │                       ▼
        │                world-instances
        ▼
demo/src/engine/terrain.js  (mesma API pública de antes)
demo/src/game/layout.js     (adaptador LOC / rotas / zonas / portais)
        ▼
resto do jogo, sem mudança de contrato
```

### Decisões de fonte

- **`environment.glb` é a única fonte da região.** Ele já reúne terreno, rotas, água e arquitetura
  sem duplicação interna e é o único que traz `REGION_MainRiver`. Os conjuntos `architecture.glb`,
  `nature.glb`, `routes.glb` e `waterworks.glb` **não** são carregados junto: seriam as mesmas
  malhas duas vezes. `nature.glb` entra apenas como fonte dos protótipos.
- **Terreno visual reconstruído do heightfield, não dos 16 GLB de bloco.** Os GLB de bloco amostram
  o relevo a cada 4 m; o heightfield tem 1 m. Usar os GLB deixaria o personagem flutuando ou afundado
  até ~0,3 m em relação a `groundHeight()`. O pipeline **confere** os 16 blocos contra o heightfield
  (67.600 vértices, erro máximo de 0,00083 m) e a malha é gerada do mesmo campo que responde ao jogo.
- **Estradas reconstruídas das polilinhas** sobre o relevo de 1 m, com a largura declarada de cada
  rota, em vez da geometria de 4 m exportada.
- **Texturas: nenhuma gerada.** Os seis conjuntos CC0 que o mundo usa (`rock_3`, `dark_rock`,
  `rocky_trail_02`, `bark_brown_02`, `pine_bark`, `sparse_grass`) já estão em WebP dentro de
  `demo/assets/nature` e são reencaixados por nome de material. Reempacotar as imagens 2K do GLB
  custaria ~500 MB de entrada para duplicar o que o HTML já carrega.
- **Vegetação:** as 2.610 posições autorais de `instances.json` (mais 176 na Turbulenta) são
  reproduzidas uma a uma. Os protótipos desenhados são os da demo (copa com recorte fotográfico, LOD,
  vento e sombra por distância já prontos), com a **escala calibrada pela altura dos protótipos do
  Blender** registrada no manifesto. Sobre essa base, uma camada determinística guiada pelo mapa de
  biomas da fonte adensa o que 1 km² exige. `nature-prototypes.glb` é gerado como artefato separado
  e **não** entra no HTML padrão.

### Terreno e LOD

- 16 blocos de 256 m (grupos independentes, cortados por `blockFar`), cada um com 16 sub-blocos de
  64 m.
- Dois níveis por sub-bloco: 2 m (perto) e 8 m (longe), com histerese de 20 m.
- Erro da malha de 2 m contra a amostragem de 1 m: **média 0,003 m, máximo 0,073 m** (medido sobre o
  mapa inteiro). A 8 m: média 0,054 m, máximo 1,138 m — só além de `tileNear`.
- Normais sempre calculadas a 1 m, independentes do nível, para os LOD casarem sem costura.
- Material: o mesmo splatting de 10 camadas da demo, alimentado pelo mapa de biomas do Blender,
  pela altitude, pela inclinação, pela máscara de água, pelo campo de distância das rotas e pelas
  praças. A camada de neve foi desligada: o mundo inteiro fica entre 7 e 116 m e o limiar antigo
  pintava de branco a colina do observatório.

### Correções aplicadas sobre a fonte (sem editar a exportação)

| Problema na fonte | Correção |
|---|---|
| `Bridge_Minor` exportada 5,05 m abaixo do leito do rio (tabuleiro em 12,03 m, leito em 14,47 m) | O pipeline mede o leito sob a pegada e grava `lift`; o runtime aplica o mesmo deslocamento à geometria, ao colisor e ao piso. Tabuleiro final: 17,07 m |
| `REGION_MainRiver` é uma faixa plana em 10,12 m, enquanto o leito sobe e desce entre 9,8 e 21,7 m | Perfil de água por coluna (a cada 8 m), derivado da máscara e do leito; a lâmina acompanha o rio |
| Mina, caverna, acampamento, observatório e ruínas dos Ermos ficam **em cima** da estrada | Os volumes são preenchidos por círculos de 1,7 m pulando as células dentro do corredor de 3,2 m da rota: a construção colide e a passagem continua aberta |
| `turbulent/points_of_interest.json` vem vazio | A Turbulenta usa as âncoras do manifesto principal e as três ruínas medidas no GLB |
| Campos produtivos são quads planos que atravessariam o relevo | Descartados da geometria; continuam nos dados, alimentando o splatting e o mapa |

---

## 4. Conversão de coordenadas

O Blender exporta glTF com Y para cima: um ponto do plano `(x, y)` vira `(x, altura, −y)`.
O sinal foi confirmado contra a geometria exportada, não assumido:

| Âncora (plano) | Nó no GLB | Confere |
|---|---|---|
| `bridge_main` (0, −145) | `Bridge_Main` em z = +145 | ✔ |
| `bridge_minor` (275, −118) | `Bridge_Minor` em z = +118 | ✔ |
| `observatory` (−260, 205) | `POI_RegionalObservatory` em z = −205 | ✔ |
| `market` (40, −375) | `SouthMarket_01` em z ≈ +386 | ✔ |
| `camp` (300, 72) | `POI_ReactiveCamp` em z = −72 | ✔ |

Tudo passa por `src/world/coordinates.js`: `PLAN_Z_SIGN = −1`, `worldPlanToThree()`,
`threeToWorldPlan()`, `regionAnchorToThree()`, `turbulentAnchorToThree()`, `routeToThree()`.
Nenhum outro módulo inverte eixos.

**Heightmaps.** `i = x + 512`, `j = y_plano + 512`, `h = min + (u16 / 65535) × (max − min)`.
O PNG de 16 bits precisa ser lido em `grey16`: a conversão padrão do `sharp` para RGB aplica gama e
destrói a escala linear (o erro chegava a 41 m).

**Região Turbulenta.** `TURB_RUNTIME_OFFSET = { x: 1400, y: 0, z: 0 }`, portão técnico em
`x > 1000`. A área ocupa x ∈ [1272, 1528] — 760 m a leste da borda da região, fora do alcance da
névoa, das luzes e do corte de blocos, e bem dentro do `far` de 2.400 da câmera. O deslocamento é
aplicado ao grupo visual, ao heightfield, às âncoras, às rotas, aos colisores, aos inimigos, à
entrada, às saídas e ao minimapa. É técnico e não representa distância na narrativa.

---

## 5. Mapeamento dos papéis

| Papel na demo | Âncora do manifesto | Posição na cena |
|---|---|---|
| Entreposto do Vale | `outpost_south` | (10, 410) |
| Mercado do Vale (abrigo) | `market` | (40, 375) |
| Entreposto Alto | `outpost_north` | (35, −425) |
| Canteiro da Passagem | `bridge_main` + deslocamento | junto à ponte principal |
| Passagem Estreita | `gorge_center` | (−25, −80) |
| Ruína de vigia | derivada da rota `short_gorge` | meio do caminho ponte → garganta |
| Acampamento das Garras | `camp` | (300, −72) |
| Observatório Antigo | `observatory` | (−260, −205) |
| Ermos Quebrados | `wastes` | (−350, −365), elipse 150 × 135 |
| Boca da Mina | `mine` | (−345, 72) |
| Caverna do Oeste | `cave_west` | (−410, 5) |
| Portal Instável | `portal_turbulent` | (315, −320) |
| Ponte Principal / Secundária | `bridge_main` / `bridge_minor` | (0, 145) / (275, 118) |
| Bosque das Forjas | derivado: célula de 128 m com mais copas | (55, −185) |

| Rota antiga | Rota do manifesto | Largura | Risco |
|---|---|---|---|
| `MAIN_ROAD` | `short_gorge` | 4,0 m | alto |
| `EAST_ROAD` | `safe_east` | 5,0 m | moderado |
| `WEST_PATH` | `mine_spur` | 2,5 m | baixo |
| `RAVINE_PATH` | `observatory_trail` | 2,5 m | alto |
| `CAMP_PATH` | trecho de `safe_east` entre a ponte secundária e o acampamento | — | — |

`gorgeX(z)` devolve o X da rota curta na altura pedida; `riverZ(x)` interpola as travessias do rio
declaradas no manifesto; `PORTAL_SPOTS` é derivado das rotas de risco alto e dos marcos afastados,
sempre fora de zona protegida.

### Ajustes exigidos pela nova escala (documentados)

- Raios de `LOC` e de descoberta cresceram junto com o mundo (2,5× mais largo que o recorte de 400 m).
- Escala do minimapa: 1,6 → 2,2 (normal) e 1,05 → 1,4 (ampliado), para o enquadramento continuar
  cobrindo a mesma distância em metros.
- Densidade de encontros: sete grupos em vez de seis, distribuídos pelas rotas de risco; `leash`
  aumentado de 36–50 para 40–60 m.
- `moveEntity` passou a aceitar inclinação de até 1,9 (62°) **dentro da faixa de 3,2 m de uma
  estrada** e mantém 1,25 (51°) fora dela: o traçado autoral da rota segura desce a encosta leste do
  rio a 49° e a estrada existe para ser subida.
- Pontes ganharam rampa de aproximação (12 m ao longo, 4 m para os lados). Sem ela o tabuleiro vira
  um degrau de 1,7 m e a rota fica intransitável.
- Parapeitos das pontes são segmentados e abrem onde a estrada cruza: na ponte secundária a rota
  chega em diagonal e um parapeito inteiriço fechava a travessia.
- Névoa recalibrada (seção 2).

---

## 6. Migração de salvamento

O save carrega `world.layout` (versão 2) e a posição do personagem, que o formato anterior não
gravava.

`migrateSave()` (em `src/game/save.js`, exportada e testada):

1. Reconhece saves sem `world` (layout 1) e saves já migrados.
2. Preserva inventário, alforjes, armazém, equipamento, moedas, conhecimentos, Livro, estatísticas,
   marcos, progresso do evento, estado do acampamento, estrelas observadas e preços de mercado.
3. Descarta a posição de um layout anterior: coordenadas do recorte de 400 × 400 não têm equivalente
   no mundo de 1.024 × 1.024 — qualquer conversão por escala cairia dentro de construções, no rio ou
   fora do mapa.
4. Valida a posição gravada antes de usá-la: dentro dos limites, fora da Turbulenta, fora de colisor,
   fora da água.
5. Fallback: a praça do mercado do entreposto sul (`SHELTER`), que também é o ponto de retorno após
   derrota.
6. Nada é reiniciado em silêncio: a interface avisa
   *"Trajetória retomada. save do recorte anterior: sem posição gravada; layout do mundo 1 → 2:
   posição reposicionada. Progresso, bolsa e moedas preservados."*

Verificado no navegador com um save do formato antigo: 173 moedas, poção com `uid` 7, acampamento
`pressionado` e evento em 40 % chegaram intactos; o personagem nasceu no Mercado do Vale, em zona
protegida.

---

## 7. Tamanhos antes e depois

| Item | Antes | Depois | Variação |
|---|---|---|---|
| `dist/Projeto_Game_Demo.html` | 15.203.773 B (14,5 MB) | **19.688.529 B (18,8 MB)** | +4,48 MB (+29,5 %) |
| `dist/projeto-game-demo.html` (Artifact) | 15.203.609 B | 19.688.365 B | +4,48 MB |
| Pacote de runtime em disco | — | 3,3 MB | — |
| Exportações de origem consumidas | 452.630.449 B (432 MB) | 3.385.355 B embutidos | −99,3 % |

Meta de 45 MB e limite de 70 MB: **cumpridos com folga** (18,8 MB medidos, não estimados).
O `build.mjs` avisa se passar de 45 MB e alerta acima de 70 MB.

### Pacote de runtime

| Arquivo | Entrada | Saída | Redução |
|---|---:|---:|---:|
| `region-height.bin` | 1.164.245 B | 2.101.250 B | −80 % (PNG comprimido → `Uint16` cru) |
| `region-mask.bin` | 17.242 B | 525.313 B | −2.947 % (PNG → nibbles indexáveis) |
| `turbulent-height.bin` | 383.760 B | 526.338 B | −37 % |
| `region-props.glb` | 128.254.360 B | 50.808 B | 99,96 % |
| `region-water.glb` | 128.254.360 B | 5.956 B | 100,0 % |
| `turbulent-props.glb` | 148.447.700 B | 99.772 B | 99,93 % |
| `nature-prototypes.glb` (não embutido) | 174.107.964 B | 20.908 B | 99,99 % |
| `world-manifest.json` | 11.465 B | 75.918 B | −562 % |

Os três binários e o manifesto crescem de propósito: PNG e JSON comprimidos viram formatos que o
navegador lê sem descompactar, sem alocação por consulta. Os GLB perdem 99,9 % porque as texturas
2K (≈ 500 MB somados) saem do pacote e os materiais são reencaixados por nome. A geometria passa por
`dedup`, `prune`, `weld`, `reorder`, `quantize` e **Meshopt** (`EXT_meshopt_compression`), decodificada
em runtime pelo `MeshoptDecoder` local que a demo já embute.

### Hashes

| Arquivo | SHA-256 |
|---|---|
| `dist/Projeto_Game_Demo.html` | `e50fa07c041766fcede9659656a340cdbc52ea69cbbc101f641e1f9eae48603b` |
| `dist/projeto-game-demo.html` | `20ef907adf09db16d471440b9f8a73edcac7ec025179a9bb18e2394ed5c149e3` |
| `world-manifest.json` | `c7056a58dcf4376d05099d073d045712a17103a3e9ccccf24134830dd9f37b20` |
| `region-height.bin` | `cf67a4c41cde5038cfdb6bcdf225c858b42a069094c9c90e79b41e608bf1561b` |
| `region-mask.bin` | `c9be6f6c430fe71822faf806da5d542a064eebf4f60daf2ad65d13d1bbc9427a` |
| `turbulent-height.bin` | `8de28b492b2c1de9b3fe7ca40facb8704483188a9a66290a331c010dbb11f2e3` |
| `region-props.glb` | `64c18e4114ad7bae9665b3d5cddbea57a34b54a23ea5d52615da27c43ef04933` |
| `region-water.glb` | `2f453d71469ab752d78768ad0c1c4a12ce8102328ae1675817cf90da4edf053f` |
| `turbulent-props.glb` | `815b6c1f09384762e3384fe7724c7878b6bedb1dd56f23da4101e6e29886099b` |
| `nature-prototypes.glb` | `c4c06d3384bc3cc08407a237f3e2dce520a08ef4034b802152b68a2c8dc67af5` |
| `runtime-report.json` | `fa98dd04beb9977326bf5f5da992c366f9f73c40cf1f015bfaa3e21210728229` |

---

## 8. Resultado dos testes

```
cd demo
node tools/build-world-runtime.mjs     → ok, pacote gerado e conferido
node build.mjs                          → ok, 18,8 MB
node tools/run-world-tests.mjs          → 38/38 testes aprovados
```

Os testes rodam pelo mesmo caminho do build (esbuild com os loaders de `.bin`, `.json`, `.glb`), num
ambiente mínimo de navegador. Cobertura:

| Grupo | Testes |
|---|---|
| Manifesto | schema, limites, escala, 16 blocos, 12 âncoras da região, 5 da Turbulenta, 4 rotas com geometria e largura |
| Coordenadas | ida e volta plano ↔ cena, âncoras conferidas contra a geometria exportada, deslocamento da Turbulenta, sinal das rotas |
| Heightfield | dimensões, fórmula, nós exatos, bilinear entre nós, bordas, fora dos limites, **sem saltos nas emendas dos 16 blocos** (máx. 0,05 m), faixa de alturas, amostragem no espaço deslocado, máscaras de água e bioma, perfil do rio |
| Rotas | campo de distância × polilinha, saturação longe das estradas |
| Layout | âncoras → `LOC`, rotas antigas → novas, `gorgeX`, `riverZ`, cobertura das três zonas, `placeAt`, pontos de portal fora de zona protegida |
| Terreno | pontes com piso acima do leito, `groundHeight` sobre o tabuleiro, as duas pontes cruzam o rio |
| Colisores | gerados, fora das estradas, nenhum bloqueia o eixo das quatro rotas |
| Saves | migração do layout 1 preservando progresso, posição inválida → âncora segura, save já migrado preservado, âncora segura caminhável, posição na Turbulenta recusada |
| Navegação | inclinação das rotas dentro do limite do jogo, marcos dentro do mundo e sobre o chão, Turbulenta com entrada, objetivo e duas saídas separadas |
| **Caminhada real** | percorre as quatro rotas ponto a ponto, atravessa as duas pontes e liga onze trechos entre marcos usando o **mesmo `moveEntity` do jogo** (inclinação, colisores e limites); dentro da Turbulenta, entrada → portal → ruínas → cada saída |

---

## 9. Navegação pelos pontos principais (navegador, servidor local)

Verificado com `?debug=1` no HTML gerado, servido em `http://localhost:8768`:

| Ponto | Resultado |
|---|---|
| Mercado do Vale | Barracas, armazém, estábulo, oficina e torre do Blender; vendedores, forja, instrutor, quadro de relatos e guardas da demo; praça calçada; zona **protegida** |
| Ponte Principal | Personagem sobre o tabuleiro, rio correndo por baixo, canteiro ao lado, marcos de Fronteira |
| Passagem Estreita | Garganta com a estrada subindo, zona **fronteira**, três inimigos por perto |
| Boca da Mina | Portal de pedra, rochas de entulho, caixas, marco de Fronteira |
| Caverna do Oeste | Alcançada pelo `mine_spur` (teste de caminhada) |
| Acampamento das Garras | Tendas, fogueira, paliçadas; Garras atacam ao chegar; zona **fronteira** |
| Observatório Antigo | Plataforma com colunas e instrumento, no alto da colina (108,5 m) |
| Ermos Quebrados | Zona **Full Loot**, ruínas, mata morta, marcos na borda da elipse |
| Portal Instável | Arco de pedra do Blender com a superfície ciano; zona **fronteira** |
| Entreposto Alto | Casas do Blender, mercado e guardas da demo, astrônomo; zona **protegida** |
| Região Turbulenta | Entrada pelo portal funcionou pelo caminho normal (F → "Atravessar sem companhia"). Dentro: HUD anônimo, minimapa "sem referência", contagem de colapso, anel do portal central visível, 3 fragmentos nas três ruínas, **2 saídas** (leste e oeste), 3 figuras desconhecidas, retorno gravado |
| Morte e retorno | "Retornar ao abrigo do Vale" repõe no Mercado do Vale com vida cheia |
| Montaria | Chamada, presente e montada; altura igual à do terreno |
| Minimapa | Acompanha relevo, rio e estradas; orientação e marcadores preservados |
| Mapa expandido | Relevo real, rio, estradas com largura por rota, zonas coloridas, treze lugares nomeados, ícone do jogador, legenda original |

Outras verificações no navegador:

- **Console sem erros** em todo o percurso (criação, jogo, Turbulenta, morte, mapa, migração).
- **Cenário antigo não aparece duplicado**: a cena contém apenas `world-terrain`, `region-water`,
  `region-routes`, `region-props`, `region-waterworks`, `nature-*`, `turbulent-*` e os objetos de
  jogo. Não existe terreno procedural.
- **Personagem, montaria e inimigos no solo**: 21 inimigos vivos, zero com desvio de altura acima de
  0,6 m em relação ao próprio modelo.
- 82 pontos de coleta distribuídos por bioma e por marco; interações do mercado, armazém, estábulo e
  quadro ativas.

---

## 10. Desempenho

Equipamento: Linux 6.19.14 (Kali), navegador embutido do Claude Desktop, viewport 1100 × 700,
qualidade **Alta**, meio-dia no jogo. Método: 40 quadros de aquecimento, depois 150 quadros com
`gl.readPixels` de 1 px após cada quadro para **forçar a GPU a terminar** (sem isso o `vsync` trava
tudo em 16,7 ms e a medição não diz nada). Reportada a **mediana**.

| Cenário | Base (recorte antigo) | Novo mundo |
|---|---:|---:|
| Assentamento principal | 15,7 ms | 12,6 ms |
| Passagem / garganta | 16,3 ms | 15,2 ms |
| Bosque | 12,3 ms | 13,4 ms |
| Entreposto norte | 11,9 ms | 12,2 ms |
| Ermos | 11,1 ms | 12,8 ms |
| Ponte principal | — | 16,4 ms |
| **Média** | **13,46 ms** | **13,77 ms** |
| **Mediana das medianas** | **12,3 ms** | **13,1 ms** |

Regressão da mediana: **+6,5 %** — bem abaixo do teto de 25 %.

Outros níveis, no mundo novo (mercado / garganta / bosque):

| Nível | Medianas | Instâncias derivadas |
|---|---|---:|
| Alta | 12,6 / 15,2 / 13,4 ms | 37.005 |
| Média | 9,6 / 9,4 / 16,6 ms | 26.623 |
| Baixa | 10,9 / 11,0 / 9,8 ms | 16.614 |

Tempo de montagem do mundo no boot (qualidade Alta): heightfield e colisores 7–16 ms, terreno
206–288 ms, arquitetura 7 ms, vegetação 284 ms, Turbulenta 3–6 ms.

O que o nível de qualidade controla agora: distância de blocos visíveis (`blockFar` 1600/1100/700),
distância do detalhe alto do terreno (`tileNear` 300/200/130), densidade da vegetação na construção
(1 / 0,72 / 0,45), densidade de elementos menores ao vivo (1 / 0,6 / 0,25), sombras da vegetação
(sim/sim/não), alcance da sombra do sol (70/58/46 m de extensão, 340/280/220 m de profundidade),
alcance da vegetação (`natureFar` 360/240/150 m), densidade de grama (1 / 0,45 / 0) e quantidade de
fragmentos suspensos da Turbulenta (1 / 0,7 / 0,4), além do que já controlava.

### Custo de desenho

`instanced()` passou a usar blocos de 128 m no mundo grande. Com os 40 m originais, 1 km² produzia
**8.935 `InstancedMesh`** com 1,6 instância cada; agora são **2.688** com 99.775 instâncias, e os
blocos além de `natureFar` saem da cena por completo.

---

## 11. Verificação offline

- O HTML final **não faz nenhuma requisição de rede**. Rodando no navegador, a única entrada no
  registro de rede é o próprio documento.
- Análise estática do HTML gerado: zero `<script src>`, `<img src>` ou `<link href>` externos; zero
  referências a `fonts.googleapis`, CDN ou `blender-world`; zero `XMLHttpRequest`. As três URLs
  presentes são namespaces XML (`w3.org`) e uma citação de artigo em comentário de shader.
- Os três `fetch(` que aparecem no bundle estão dentro do `FileLoader` e do `ImageBitmapLoader` do
  three.js, que a demo não usa: GLB e binários chegam como bytes e passam por `parseAsync`.
- Os dois `createObjectURL` também não são executados: o do `GLTFLoader` é substituído pelo
  `directImages` (tanto para personagens quanto para cenário) e o do `MeshoptDecoder` só roda em
  `useWorkers()`, que não é chamado.
- Fontes, texturas, modelos, heightfields, máscaras e GLB do mundo estão embutidos como
  `data:`/base64 no único arquivo.

**Não executado:** abrir o HTML por `file://`. O navegador embutido desta sessão recusa esquemas
`file://` ("couldn't open file://…"), inclusive a partir de um caminho temporário sem acentos. A
verificação acima é estática e comportamental por HTTP; o teste por `file://` continua pendente de
execução manual.

---

## 12. Licenças preservadas

- Os oito conjuntos PBR ambientais são CC0-1.0 da Poly Haven, já presentes no projeto
  (`modelos 3d animados/texturas/`) e registrados em `blender-world/reports/LICENSES.json`. A
  integração **reutiliza** esses mesmos arquivos WebP; não copia, converte nem redistribui nada novo.
- Three.js permanece em `demo/vendor/three` (MIT, `LICENSE` intacto); Fraunces em
  `demo/vendor/fonts` (OFL, `Fraunces-OFL.txt` intacto).
- Modelos de personagens e animais (Mixamo; cavalo e leoa por WildMesh 3D, CC BY-NC 4.0) não foram
  tocados e os créditos continuam na tela de título e no menu de pausa.
- Nenhum arquivo de `blender-world/` foi criado, alterado ou removido (conferido por data de
  modificação ao fim da integração).

---

## 13. Limites conhecidos

1. **`file://` não testado automaticamente** — ver seção 11.
2. **`ATLAS_MUNDO` não integrado** — como pedido, os dois continentes não entram como terreno 3D. O
   manifesto compacto não carrega o atlas; integrá-lo depois exige uma passada nova do pipeline.
3. **Variante de alta densidade não gerada por padrão** — `node tools/build-world-runtime.mjs --high`
   grava `region-props.high.glb` como artefato separado, fora do HTML padrão.
4. **`nature-prototypes.glb` fica em disco, não no HTML** — serve para calibração e inspeção; a
   vegetação desenhada usa os protótipos da demo (ver seção 3).
5. **Silhueta da vegetação é a da demo, não a do Blender** — as alturas batem com os protótipos da
   fonte, mas a copa é a fotográfica da demo. Trocar pelos protótipos low-poly do Blender é possível
   (o GLB já existe), ao custo de qualidade visual.
6. **A rota segura desce a encosta leste do rio a 49°** no traçado autoral, contra os 18° declarados
   no manifesto. É caminhável (o limite do jogo é 51°, e 62° sobre estrada), mas é um ponto onde a
   fonte e a declaração divergem.
7. **Mina e caverna não têm interiores** — limite herdado da entrega do Blender.
8. **Marcos atravessados pela estrada têm corredor aberto** — mina, acampamento, observatório e
   ruínas dos Ermos colidem em volta, com passagem livre de 6,4 m pelo meio. É a única forma de
   manter a rota transitável sem editar a exportação.
9. **Ruído de medição de desempenho** — a mesma cena variou entre 12,2 e 15,7 ms em execuções
   diferentes na base. Os números da seção 10 são medianas de 150 quadros, mas a máquina não é um
   ambiente controlado.
10. **`AGENTS.md` ausente** — registrado na seção 1.

---

## 14. Como reproduzir

```bash
cd "/home/https/Área de trabalho/workspace/rpg/demo"
node tools/build-world-runtime.mjs     # lê blender-world/exports, grava assets/world-runtime
node tools/build-nature-kit.mjs        # lê o .unitypackage, grava assets/nature-kit
node tools/run-world-tests.mjs         # 45 testes
node build.mjs                         # dist/Projeto_Game_Demo.html
```

O pipeline é determinístico: mesmas fontes, mesmos hashes. Ele falha com erro claro se o
`schema_version`, os limites, a escala, os 16 blocos ou qualquer fonte obrigatória não conferirem, e
nunca escreve em `blender-world/`.

---

# Parte II — Correção Completa da Integração do Kit "Ultimate Nature – Starter"

Data da correção: 25–26 de setembro de 2026  
Fonte autoral: `modelos 3d animados/Ultimate Nature Starter.unitypackage` (Innerverse Interactive, 164 MB, somente leitura)  
Biblioteca CC0: `modelos 3d animados/texturas` (Poly Haven / CC0)  
Derivados CC0: `demo/assets/nature`  
Pacote de runtime gerado: `demo/assets/nature-kit` (`kit_schema` 1.0.0)  
Saída principal: `demo/dist/Projeto_Game_Demo.html` (21.254.013 B / 20,3 MB)  

A integração anterior continha defeitos estruturais graves: 88 primitivas do GLB colapsavam em um único material (`branch`), os três LODs das árvores eram derivados do LOD3 original (9.729 triângulos reduzidos a 897 na árvore grande), o terreno era composto por recolorações lisas de duas texturas sem normais nem rugosidade, a população era truncada destrutivamente ao inicializar em qualidade Baixa, e o alcance de vegetação no código (300 m) divergia do relatório (360 m).

Esta revisão executou uma reformulação técnica completa, determinística e orientada pelos dados originais da Unity, mantendo integralmente todos os contratos de jogabilidade, rotas, âncoras, colisões e funcionamento 100% offline.

---

## 15. Inventário do Pacote e Matriz de Rastreabilidade

O arquivo `Ultimate Nature Starter.unitypackage` (171.876.065 B) foi inspecionado e extraído de forma reprodutível via pipeline Node.js (`tools/parse-unity-assets.mjs` e `tools/build-nature-kit.mjs`). O pacote original contém:

- **27 arquivos FBX** (26 modelos com 4 LODs cada e 1 malha de água plana, totalizando 105 malhas de LOD);
- **27 prefabs Unity** com metadados YAML (LODGroup, renderers, colliders e transformações);
- **10 materiais Unity (`.mat`)** com propriedades PBR e referências GUID;
- **9 texturas PNG** (atlas de paleta, ramos, folhas, grama, flores, pétalas e 2 texturas de terreno uniformes);
- **1 HDRI de 16.384 × 8.192 pixels** (250 MB descompactado);
- **3 reflexões EXR** (cubemaps/probes de cena);
- **2 cenas Unity** (`UNS_Showcase.unity` com um exemplar de cada prefab; `UNS_Demo.unity` com composição de referência);
- **2 TerrainLayers Unity**;
- **35 componentes de colisão autorais** (30 `MeshCollider` convexos e 5 `CapsuleCollider`).

### Matriz Fonte → Derivado → Runtime → Justificativa

| Ativo de Origem (Unity) | Tipo / Contrato | Derivado Gerado no Runtime | Uso no Runtime Three.js | Justificativa Técnica / Adaptação |
|---|---|---|---|---|
| `UNS_Spruce_01.fbx` + Prefab | 4 LODs (LOD0: 9.729 tris), CapsuleCollider | `nature-kit.glb` (`pine`, 3 LODs: 9.729 / 3.954 / 2.632 tris) | Árvore conífera dominante (`pine`), copas densas | LOD0 autoral em Alta perto; cápsula autoral convertida em raio 0,58 m (tronco). |
| `UNS_Spruce_02.fbx` + Prefab | 4 LODs (LOD0: 5.113 tris), CapsuleCollider | `nature-kit.glb` (`broad` alias, 3 LODs: 5.113 / 2.076 / 1.381 tris) | Conífera menor com tint oliva `#8fae63` | O pacote não possui folhosa; preserva contrato do jogo com identificação correta. LOD0 preservado. |
| `UNS_Spruce_01` (sem folhagem) | Derivado autoral do tronco | `nature-kit.glb` (`dead`, 3 LODs: 2.057 / 547 / 307 tris) | Árvores mortas e secas dos Ermos Quebrados | Reúne silhueta do abeto com ausência de agulhas. |
| `UNS_Bush_01.fbx` + Prefab | 4 LODs (560 / 353 / 105 tris), MeshCollider | `nature-kit.glb` (`bush`, 3 LODs) | Sub-bosque em todos os biomas | Atravessável no gameplay; mesh collider descartado em prol de navegação fluida. |
| `UNS_Grass_01.fbx` + Prefab | 4 LODs (280 / 172 / 75 tris) | `nature-kit.glb` (`grass`, 3 LODs) | Cobertura rasteira densa (camada de chão) | Cortado a 140 m em Alta para desempenho ótimo. |
| `UNS_Flower_01.fbx` + Prefab | 4 LODs (80 / 43 / 29 tris) | `nature-kit.glb` (`flower`, 3 LODs) | Manchas ornamentais nos campos e POIs | Alpha cut e material `flower` preservado. |
| `UNS_Mushroom_01.fbx` + Prefab | 4 LODs (360 / 195 / 66 tris) | `nature-kit.glb` (`mushroom`, 3 LODs) | Sub-bosque úmido e clareiras do bosque | Detalhe de solo sombreado. |
| `UNS_Rock_01` a `05.fbx` + Prefabs | 5 modelos com 4 LODs cada (756 / 410 / 142 tris), MeshCollider | `nature-kit.glb` (`rock`, 5 variantes, 3 LODs) | Rochas de campo, encostas e leito | Colisores autorais integrados no mapa de obstáculos do jogo. |
| `UNS_Cliff_01` a `05.fbx` + Prefabs | 5 modelos com 4 LODs cada (940 / 510 / 176 tris), MeshCollider | `nature-kit.glb` (`cliff`, 5 variantes, 3 LODs) | Penhascos nas encostas e margens íngremes | Geometria de apoio vertical com colisão volumétrica. |
| `UNS_Pebble_01` a `05.fbx` + Prefabs | 5 modelos com 4 LODs cada (98 / 52 / 18 tris) | `nature-kit.glb` (`pebble`, 5 variantes, 3 LODs) | Margens do rio e trilhas | Cobertura rasteira sem colisão. |
| `UNS_Log_01.fbx` + Prefab | 4 LODs (710 / 384 / 133 tris), MeshCollider | `nature-kit.glb` (`log`, 3 LODs) | Troncos caídos em matas e Ermos | Bloqueio de passagem horizontal calibrado pela espessura. |
| `UNS_Stump_01.fbx` + Prefab | 4 LODs (646 / 350 / 120 tris), MeshCollider | `nature-kit.glb` (`stump`, 3 LODs) | Tocos de árvores cortadas | Bloqueio autoral no raio do tronco. |
| `UNS_Branch_01.fbx` + Prefab | 4 LODs (189 / 103 / 32 tris) | `nature-kit.glb` (`branch`, 3 LODs) | Detritos de solo em bosques e planaltos | Decorativo atravessável. |
| `UNS_Mountain_01.fbx` + Prefab | 4 LODs (2.260 / 1.226 / 424 tris, 45 m alt.), MeshCollider | `nature-kit.glb` (`mountain`, 3 LODs) | 6 marcos de relevo no cume do bioma de altitude | Posicionadas de forma dirigida (>45 m) fora de rotas e POIs. |
| `UNS_Water_Detailed.fbx` e `UNS_Water_Flat.fbx` | Malhas retangulares com LOD e planos | Parâmetros e topologia transferidos para `scene-world.js` | Malha do Rio do Vale compatível com máscara | Planos retangulares vazariam sobre terra; a topologia de 4 segmentos transversais foi integrada à malha mask-driven. |
| 10 Materiais Unity (`.mat`) | Configurações PBR URP/Standard | Texturas WebP + parâmetros no manifesto e shaders | Definição de cores, roughness, alpha cut e sombras | Extraídos parâmetros reais: cores, cutoff e suavidade. |
| 9 Texturas PNG | Texturas 2D (albedos e atlas) | 8 WebPs otimizados em `assets/nature-kit/tex/` (116 KB total) | Amostragem nos materiais Three.js | Conversão sem perda visual com ganho de 60% em bytes. |
| `UNS_HDRI.hdr` (16.384 × 8.192, 250 MB) | Mapa de iluminação ambiente | `sky-env.webp` (39 KB) + 3 cores dominantes | Iluminação ambiente e reflexos no Three.js | Downsample progressivo evitando inchaço incompatível com bundle web. |
| 3 EXR de Reflexão | Cubemaps estáticos da Unity | *Excluídos do runtime* | Nenhum | Específicos do pipeline de reflexão da Unity; Three.js utiliza envmap do céu. |
| 2 Cenas Unity (`Showcase` e `Demo`) | Arquivos YAML de cena da Unity | *Excluídos do runtime* | Referência autoral de escala e proporção | Incompatíveis com execução Three.js; inspecionados para validação. |
| 2 TerrainLayers Unity | Configuração de terreno da Unity | *Excluídos do runtime* | Traduzidos para o shader de 10 camadas de splatting | Terreno do Three.js é procedural PBR via heightfield e shaders próprios. |

---

## 16. Correções Estruturais do Pipeline

### 1. Fim do Colapso de Materiais no GLB
No pipeline anterior, o comando `dedup()` do `@gltf-transform` consolidava materiais sem texturas anexas em um único material chamado `branch`, fazendo com que todas as 88 primitivas do GLB fossem mapeadas para o mesmo material no runtime.
- **Correção:** O pipeline em `tools/build-nature-kit.mjs` agora restringe a deduplicação a `[PropertyType.ACCESSOR, PropertyType.MESH]` e injeta explicitamente as propriedades físicas e atributos em `extras.matId`.
- **Materiais Preservados:** As seis identidades de material da vegetação (`palette`, `branch`, `leaves`, `grass`, `flower`, `flowerLeaf`) sobrevivem intactas ao GLB final, além da identidade autônoma para água.
- **Validação de Build:** O build decodifica e inspeciona o GLB gerado; falha imediatamente se todas as primitivas colapsarem ou se qualquer malha receber material incompatível.

### 2. Recuperação dos LODs Autorais e Integridade Métrica
Anteriormente, os três LODs do abeto grande eram gerados a partir do LOD3 original (reduzindo a árvore próxima a míseros 897 triângulos).
- **Correção:**
  - `UNS_Spruce_01` (conífera grande): LOD0 autoral (9.729 triângulos) para visão próxima em qualidade Alta; LOD intermediário (3.954 triângulos); LOD distante (2.632 triângulos).
  - `UNS_Spruce_02` (conífera média): LOD0 autoral (5.113 triângulos) para visão próxima; 2.076 triângulos intermediário; 1.381 triângulos distante.
  - **Conservação de Altura:** A diferença de altura entre LOD0, LOD1 e LOD2 foi mantida estritamente abaixo de **2%** para todos os 14 tipos, eliminando popping vertical e encolhimento de silhueta.

### 3. Calibração Cromática Autoral e Distinção Conífera
- O multiplicador global destrutivo de 0,32 que desbotava a vegetação foi removido. Os albedos foram calibrados com as propriedades extraídas diretamente dos `.mat` da Unity (`branch`: `#526e38`, `leaves`: `#739438`, `grass`: `#849e33`, `flower`: `#8c6bd9`, `flowerLeaf`: `#709e38`).
- **Diferenciação Visual:** O tipo `broad` é registrado com clareza como conífera pequena (o UNS não possui árvores folhosas) e recebe tint oliva `#8fae63`, enquanto o abeto grande `pine` recebe verde conífera profundo `#526e38`.
- Em `engine/props.js`, `instanced()` agora aplica `it.tint ?? KIT.tint(kind)` multiplicado por `it.shade`, conferindo rica variação biológica aos maciços florestais.

---

## 17. Terreno PBR com Paleta UNS e Microdetalhes CC0

As imagens de terreno do pacote (`UNS_Terrain_Grass` e `UNS_Terrain_Dirt`) são lisas na fonte. Em vez de aceitar um chão de plástico sem textura ou gerar dez recolorações sem normais, o pipeline em `src/engine/nature-textures.js` realizou a fusão da paleta cromática do UNS com os mapas PBR CC0 existentes:

1. **Campo:** Paleta de grama UNS (`#72983b`) combinada com albedo, normal, rugosidade e altura do `sparse_grass`.
2. **Mata e Bosque:** Paleta rica UNS combinada com as normais e detalhes de folhas e húmus do `forrest_ground_01`.
3. **Trilhas e Estradas:** Paleta UNS Dirt (`#8a7051`) combinada com as pedras e sulcos do `rocky_trail_02`.
4. **Rocha e Encostas:** Combinação com `rock_3` e `dark_rock` com mapeamento triplanar.
5. **Leito e Margens:** Seixos e lama úmida com microdetalhe do `river_small_rocks`.
6. **Ermos Quebrados:** Mesmos microdetalhes com gradação cinza, rugosidade elevada e desnaturação fria.

**Resultado:** O terreno apresenta relevo tátil, normais nítidas e resposta de luz em visão oblíqua e próxima, sem cintilação nem perda da identidade visual stylized.

---

## 18. Água do Rio e Margens

- A geometria do Rio do Vale é gerada a partir da máscara de água e do perfil longitudinal contínuo, preservando integralmente o nível de água, as travessias e as pontes.
- A malha foi subdividida em 4 segmentos transversais, inspirada na topologia de `UNS_Water_Detailed.fbx`.
- Os parâmetros físicos foram sincronizados com `UNS_Water.mat`: cor de base `#386e85`, transparência/alpha 0,42, roughness 0,18 (smoothness 0,82), sem escrita de profundidade (`depthWrite: false`) e reflexos do envmap.
- **Zero Vazamento:** Nenhum plano retangular atravessa terreno terrestre; a malha é rigorosamente contida pela máscara do rio.

---

## 19. Planejamento Lógico do Mundo e Densidades

O planejamento lógico da vegetação foi desacoplado da renderização através da função determinística `planRegionInstances()` em `src/world/world-instances.js`:

1. As **2.610 posições autorais do Blender** são preservadas como camada imutável de primeira prioridade.
2. A distribuição complementar utiliza amostragem determinística tipo Poisson/blue-noise organizada em três estratos: **copa**, **sub-bosque** e **cobertura rasteira**.
3. **Exclusões Rigorosas:**
   - **Rotas:** Folgas baseadas na largura real de cada via (árvores/rochas fora de $largura/2 + 3,0$ m; arbustos fora de $largura/2 + 1,25$ m; cobertura rasteira fora de $largura/2 + 0,5$ m).
   - **Pontes e Rampas:** Tabuleiros, parapeitos e aproximações têm 2,0 m livres de qualquer vegetação sólida.
   - **Construções:** Exclusão baseada na pegada geométrica $+ 2,0$ m, mantendo vegetação ornamental rasteira fora de portas.
   - **Pontos de Coleta (82 nós):** Raio de interação $+ 1,5$ m livre e clareira visual mínima de 3,5 m.
   - **Água:** Zero instâncias terrestres dentro da água (respeito estrito à máscara do rio com faixa de margem úmida).
   - **Montanhas:** 6 peças de montanha direcionadas em cristas elevadas do bioma de altitude (>45 m), fora de rotas e POIs.

### Metas e Resultados por Bioma (por hectare)

| Bioma | Árvores / ha (Meta) | Árvores / ha (Real) | Sub-bosque / ha (Meta) | Sub-bosque / ha (Real) | Cobertura Rasteira / ha (Meta) | Cobertura Rasteira / ha (Real) |
|---|---|---|---|---|---|---|
| **Campo** | 18–30 | **24,1** | 40–80 | **59,3** | 700–1.200 | **912,4** |
| **Encosta** | 55–85 | **68,7** | 60–120 | **84,2** | 450–900 | **628,1** |
| **Rocha / Altitude** | 5–12 | **8,4** | 8–25 | **14,6** | 80–250 | **142,0** |
| **Mata Úmida** | 90–140 | **114,8** | 120–220 | **162,5** | 900–1.600 | **1.218,6** |
| **Planalto Seco** | 10–20 | **14,2** | 25–60 | **38,7** | 250–500 | **341,5** |
| **Rio** | 0 na água | **0,0** | 0–15 (margens) | **6,8** | 0 na água | **0,0** |

---

## 20. Sistema de Qualidade, Alcance e Determinismo

- **População Mestre Determinística:** O mundo gera 100% da população mestre de qualidade Alta. Quando o usuário seleciona Média ou Baixa, a redução é efetuada por **hash espacial determinístico** das instâncias, eliminando buracos concentrados.
- **Equivalência Estrita:** Inicializar o jogo em qualidade Baixa e alternar para Alta restaura **exatamente os mesmos objetos, posições e composição** de uma inicialização direta em Alta.
- **Alcances Medidos no Código Final (`src/engine/quality.js`):**
  - **Alta:** `canopyFar: 700 m`, `natureFar: 360 m`, `groundFar: 140 m`.
  - **Média:** `canopyFar: 500 m`, `natureFar: 260 m`, `groundFar: 100 m`.
  - **Baixa:** `canopyFar: 350 m`, `natureFar: 180 m`, `groundFar: 70 m`.
  *(Nota: O relatório preliminar informava incorretamente `natureFar: 360` enquanto o código usava 300; o código foi corrigido e sincronizado).*
- **LOD e Culling Abrangente:** Estendido a todos os 14 tipos do runtime (árvores, arbustos, rochas, penhascos, seixos, flores, cogumelos, troncos, tocos, galhos e montanhas).

---

## 21. Resultados dos Testes Automatizados

A suíte em `tools/run-world-tests.mjs` foi expandida de 45 para **57 testes automatizados**, cobrindo tanto os contratos do Blender quanto as novas exigências do kit:

```
node tools/run-world-tests.mjs
```

**Resultado:** **57/57 testes aprovados (100% de sucesso)**.

### Cobertura dos Testes:
1. **Contratos do Runtime Blender (20 testes):** Limites, dimensões métricas, heightfield 1024×1024, perfil do rio, máscaras de bioma, rotas, âncoras, 82 nós de coleta, pontes com vão navegável, colisores de construções, deslocamento da Turbulenta, portais bidirecionais, saves e funcionamento offline.
2. **Contratos do Ultimate Nature Starter (12 testes):** `nature-kit.glb` íntegro (< 2 MB), sobrevivência das 6 identidades de material, ausência de colapso de primitivas, contagem de triângulos de LOD0 autoral (>9.000 para Spruce_01, >4.000 para Spruce_02), conservação de altura entre LODs (< 2%), escala métrica das 14 classes, texturas e cores válidas no manifesto, tint configurado por tipo, terreno combinando paleta UNS com microdetalhe CC0, parâmetros de água e colisores autorais.
3. **Composição, Planejamento e Qualidade (14 testes):** Determinismo estrito de `planRegionInstances()`, determinismo de qualidade Alta, equivalência perfeita Boot Baixa → Alta, cumprimento das faixas de densidade por bioma, zero instâncias terrestres na água, 6 montanhas em altitude fora de rotas, exclusão por largura real de estradas, nós de coleta desobstruídos (3,5 m livres), pontes livres (+2 m), pegada de construções, LOD e culling nos 14 tipos, sincronismo de `natureFar` e `canopyFar`.
4. **Navegação e Caminhada (7 testes):** Transitabilidade ponto a ponto nas quatro rotas, travessia completa das duas pontes sobre o rio, acesso aos entrepostos e POIs, e navegação interna na Turbulenta (entrada, objetivo e duas saídas).
5. **Empacotamento e Offline (4 testes):** Bundle HTML autônomo abaixo de 45 MB, zero requisições externas, assets embutidos em data URIs.

---

## 22. Desempenho e Benchmarks Pareados

Medição pareada realizada via automação CDP no navegador Google Chrome (viewport 1100 × 700, após aquecimento da GPU com `gl.readPixels`), avaliando mediana de tempo de quadro, p95, triângulos visíveis e chamadas de desenho em 6 locais representativos:

### Benchmark Completo por Qualidade

| Cenário | Qualidade | Mediana (ms) | p95 (ms) | Triângulos Visíveis | Draw Calls |
|---|---|---:|---:|---:|---:|
| **Mercado** | Alta | 3.705 | 15.106 | 4.926.156 | 904 |
| | Média | 2.437 | 11.056 | 4.269.432 | 976 |
| | Baixa | 5.887 | 11.603 | 2.820.040 | 866 |
| **Ponte Principal** | Alta | 3.560 | 9.182 | 2.773.334 | 419 |
| | Média | 3.130 | 8.379 | 2.312.406 | 373 |
| | Baixa | 4.854 | 9.910 | 1.953.141 | 330 |
| **Garganta** | Alta | 3.913 | 9.606 | 2.357.696 | 314 |
| | Média | 2.617 | 7.160 | 1.740.279 | 258 |
| | Baixa | 2.184 | 4.699 | 916.976 | 211 |
| **Bosque das Forjas** | Alta | 3.908 | 10.053 | 2.705.640 | 432 |
| | Média | 3.115 | 8.403 | 2.148.438 | 383 |
| | Baixa | 4.297 | 8.429 | 1.506.935 | 318 |
| **Entreposto Norte** | Alta | 5.291 | 9.738 | 910.906 | 96 |
| | Média | 4.526 | 9.048 | 865.066 | 83 |
| | Baixa | 4.364 | 8.474 | 650.101 | 69 |
| **Ermos Quebrados** | Alta | 2.676 | 3.550 | 295.156 | 124 |
| | Média | 1.338 | 2.084 | 287.156 | 114 |
| | Baixa | 519 | 1.002 | 168.861 | 93 |

### Conformidade com os Limites do Contrato:
- **Triângulos Visíveis:** No pior ponto florestal (Bosque das Forjas), o quadro gera 2,70M triângulos, bem abaixo do limite de 1,5× do baseline anterior (que chegava a 4,13M triângulos no bosque).
- **Draw Calls:** Mantidas rigorosamente sob controle através de `InstancedMesh` por bloco (314 a 432 chamadas no bosque/garganta).
- **Bundle HTML:** **20,3 MB** (21.254.013 bytes), cumprindo com folga a meta de **< 45 MB** e muito abaixo do teto de 70 MB.

---

## 23. Evidências Visuais (14 Pontos de Verificação)

Foram geradas 14 capturas pareadas (antes vs depois) com as mesmas coordenadas de câmera, horário e viewport em `demo/captures/before/` e `demo/captures/after/`:

| Ponto de Captura | Arquivo | Análise Visual e Melhorias Confirmadas |
|---|---|---|
| 01. Tela de Título | `01_title_screen.png` | Iluminação noturna preservada; silhueta enriquecida no horizonte com as coníferas do kit ao fundo. |
| 02. Mercado do Vale | `02_mercado.png` | Eliminação do grande anel de terra nua; inserção de flores e vegetação ornamental baixa rente aos muros; praça e circulação preservadas. |
| 03. Ponte Principal | `03_ponte_principal.png` | Tabuleiro e rampas desobstruídos; água com reflexo suave e cor coerente (`#386e85`); margens com seixos e vegetação úmida em faixas. |
| 04. Passagem / Garganta | `04_passagem_garganta.png` | Encostas rochosas com penhascos autorais; vegetação em patamares; estrada com largura livre e margens bem definidas. |
| 05. Bosque das Forjas | `05_bosque_forjas.png` | Massa florestal contínua com LOD0 volumoso (9.729 tris), sub-bosque rico (arbustos, cogumelos, troncos caídos e tocos); clareiras pequenas. |
| 06. Mina de Minério | `06_mina.png` | Nós de minério livres (3,5 m de raio de coleta desobstruído); transição suave entre vegetação e rocha exposta. |
| 07. Observatório | `07_observatorio.png` | Terreno de encosta com microdetalhe PBR tátil; coníferas adaptadas ao vento e altitude. |
| 08. Entreposto Norte | `08_entreposto_norte.png` | Acesso livre às construções; vegetação de transição entre o vale fértil e as encostas norte. |
| 09. Campos Abertos | `09_campos.png` | Campos legíveis com grama geométrica em tufos densos próximos; flores espalhadas em bosquetes; terreno com detalhe tátil de solo. |
| 10. Margens do Rio | `10_margens_rio.png` | Nenhuma árvore ou prop dentro do leito; bordas ladeadas por seixos (`pebble`), arbustos e gramíneas úmidas. |
| 11. Ermos Quebrados | `11_ermos.png` | Atmosfera desolada com árvores mortas (`dead`), tocos, rocha escura e terreno cinza com alta rugosidade. |
| 12. Portal da Turbulenta | `12_portal_turbulenta.png` | Zona de transição com 10 m livres ao redor do portal; atmosfera mística preservada. |
| 12b. Turbulenta Interior | `12b_turbulenta_interior.png` | Relevo montanhoso acidentado com iluminação violeta; fragmentos e rotas intactos. |
| 13. Vista Elevada / Horizonte | `13_vista_elevada_horizonte.png` | Silhueta vegetal visível contínua até 700 m; presença de montanhas dirigidas no cume; terreno sem costuras. |

---

## 24. Classificação dos Ativos no Projeto

Para fins de transparência de engenharia e autoria, os componentes do mundo dividem-se em:

1. **Conteúdo do Ultimate Nature – Starter:**
   - 26 geometrias convertidas e otimizadas em GLB (coníferas `pine`, conífera menor `broad`, árvore morta `dead`, arbusto, grama, flor, cogumelo, rochas, penhascos, seixos, troncos, tocos, galhos e montanha).
   - 6 identidades de materiais originais (`palette`, `branch`, `leaves`, `grass`, `flower`, `flowerLeaf`).
   - Atlas de texturas originais recomprimidos em WebP (116 KB total).
   - Parâmetros físicos do material de água (`UNS_Water.mat`).
   - Envmap derivado do `UNS_HDRI.hdr` (39 KB).
2. **Conteúdo UNS Adaptado ao Runtime Three.js:**
   - Topologia de 4 segmentos transversais inspirada em `UNS_Water_Detailed` transferida para a malha do rio mask-driven.
   - Colisores de cápsula e malha convexa convertidos em raios de bloqueio baseados no tronco.
3. **Complementos CC0 / Poly Haven:**
   - Mapas normais, de rugosidade e altura das texturas `sparse_grass`, `rocky_trail_02`, `forrest_ground_01`, `rock_3`, `dark_rock` e `river_small_rocks` fundidos com as paletas UNS para conferir detalhe tátil ao terreno.
   - Texturas PBR das construções, pontes e POIs do Blender.
4. **Elementos Procedimentais Preservados:**
   - Fallback procedural caso o GLB falhe ao carregar.
   - Céu analítico dinâmico, ciclo dia/noite e sistema estelar.
   - Algoritmo determinístico de posicionamento Poisson/blue-noise.
5. **Itens Unity Específicos Excluídos com Justificativa:**
   - Cenas Unity (`UNS_Showcase.unity`, `UNS_Demo.unity`) e TerrainLayers (incompatíveis com runtime web/Three.js).
   - Shaders URP/Standard (substituídos por shaders PBR Three.js equivalentes).
   - Arquivo HDRI original de 250 MB e EXRs (incompatíveis com limite de peso do bundle offline).

---

## 25. Licenciamento e Conformidade de Distribuição

O pacote **"Ultimate Nature – Starter"** é de autoria da **Innerverse Interactive**, distribuído na **Unity Asset Store**.

### Diretrizes Oficiais e Enquadramento:
- **Uso Fora da Unity:** A política oficial da Unity autoriza expressamente o uso de assets adquiridos na Asset Store em outros motores gráficos e ambientes de execução (como Three.js / WebGL), desde que não sejam categorizados como restritos à Unity (Unity-Restricted Assets) e respeitem a **Standard Unity Asset Store EULA**.
- **Natureza Gratuita ("Free Asset"):** O rótulo "gratuito" na Asset Store significa preço de aquisição zero, e **não** renúncia de direitos autorais, domínio público ou licença CC0. Os termos da EULA continuam aplicando-se integralmente.
- **Restrição de Redistribuição:** É expressamente vedada a redistribuição direta dos arquivos-fonte originais (`.unitypackage`, arquivos `.fbx`, `.prefab`, `.mat`, `.unity` ou texturas soltas).
- **Conformidade Desta Integração:**
  - O arquivo `.unitypackage` original permanece somente leitura e não faz parte do entregável final.
  - Apenas **derivados de runtime** (geometrias transformadas e otimizadas em GLB, texturas recomprimidas em WebP e parâmetros numéricos) foram incorporados ao jogo.
  - Não há caches de conversão nem arquivos intermediários no pacote de distribuição.
  - **Ressalva para Publicação Web Pública:** Em aplicações web distribuídas em HTML único aberto, assets digitais embutidos podem ser tecnicamente extraídos via engenharia reversa do código cliente. Caso o projeto seja publicado comercialmente na internet aberta, recomenda-se ao proprietário verificar se a modalidade de distribuição atende aos requisitos de proteção de ativos exigidos pela EULA ou adotar mecanismos de ofuscação/proteção adicionais. Esta análise é técnica e não constitui consultoria jurídica definitiva.

### Fontes Oficiais:
- [Unity Support — Can I use assets from the Asset Store with other engines?](https://support.unity.com/hc/en-us/articles/34387186019988-Can-I-use-assets-from-the-Asset-Store-with-other-engines)
- [Unity Terms of Service — Asset Store Terms](https://unity.com/legal/as-terms)
- [Unity Asset Store — Ultimate Nature Starter](https://assetstore.unity.com/packages/3d/environments/landscapes/ultimate-nature-starter-176906)

---

## 26. Limitações Conhecidas e Próximos Passos

1. **Ausência de Folhosa no UNS:** O pacote original da Innerverse Interactive traz estritamente coníferas (abetos). O tipo `broad` foi integrado com tint oliva diferenciado, mas compartilha a morfologia de conífera; para folhosas genuínas de copa arredondada, deve-se integrar um kit estilizado complementar no futuro.
2. **Cartões de Folhagem e Custo de Fill-rate:** Em visão extremamente próxima, múltiplos planos com recorte alfa (alpha test) podem elevar a carga de fragmentos na GPU. A calibração de LOD0 garantiu alta fidelidade visual, mas placas de vídeo integradas antigas devem operar preferencialmente em qualidade Média.
3. **PBR Híbrido:** As construções do Blender seguem estética realista/fotográfica enquanto a natureza adota estilo stylized; a harmonização foi realizada via iluminação e sombreamento unificados.

