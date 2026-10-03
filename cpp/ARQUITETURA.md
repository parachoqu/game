# Plano: reescrita em C++20 com simulação (servidor) separada de render/input (cliente)

## Contexto

Hoje o jogo é a demo em JavaScript + three.js em `demo/src` (~15 mil linhas: `game/` 20 módulos, `engine/` 19,
`world/` 21, `ui/` 6, `main.js`, `state.js`, `config.js`), empacotada num HTML offline por `demo/build.mjs`.
Os dados do mundo vêm de um pipeline offline (Blender → `blender-world/exports` → `demo/tools/*.mjs` →
`demo/assets/world-runtime`, `nature-kit`, `extra-props`, GLBs Mixamo com meshopt, texturas WebP).

O objetivo é reescrever o runtime em C++20. Por enquanto roda local, mas a arquitetura já nasce como a de
um RPG online: um núcleo de simulação autoritativo que compila sem janela, GPU, áudio ou UI (vira o
servidor dedicado sem reescrita) e um cliente que só lê input, desenha e envia intenções. O pipeline
offline (Blender/Node) continua; só ganha dois passos de exportação novos.

Este plano define camadas, regras e cada arquivo `.h/.cpp`. Nenhum código é escrito nesta etapa.

---

## 1. Diagnóstico: onde o código atual mistura simulação e apresentação

Estes são os acoplamentos que a reescrita precisa quebrar. Eles guiam a divisão de arquivos.

| # | Problema no JS | Onde | Como fica no C++ |
|---|---|---|---|
| 1 | A lógica chama a apresentação direto: `toast`, `sfx`, `floatText`, `burst`, `telegraph`, `showBar` | `combat.js`, `player.js`, `enemies.js`, `economy.js`, `camp.js`, `event.js`, `stars.js`, `turbulent.js`, `interact.js`, `mount.js` | A simulação emite `GameEvent`s semânticos (id + parâmetros). O cliente decide som, partícula e texto. |
| 2 | As entidades guardam o modelo three.js (`P.model`, `e.model.m.root`, `G.scene.add` em `spawnEnemy`, `shoot`, `createLootBag`, `initTurbulent`, `world.js place()`) | game/* | A entidade da simulação só tem dados. O cliente cria uma `EntityView` por `EntityId` a partir do snapshot. |
| 3 | A simulação lê o teclado cru: `hit('KeyQ')`, `down('ShiftLeft')`, `input.mouse` | `updatePlayer`, `interact.js`, `pointer.js` | O cliente converte input em `PlayerCommand` (bits de botões + direção + mira). |
| 4 | A mira faz raycast com a câmera (`G.camera`) dentro do update do jogador | `player.js updateAim` | O cliente calcula a mira e envia `aimPoint`/`aimYaw`/`aimPitch`. O servidor valida alcance. |
| 5 | Estado global `G` com um único `G.player`: IA, combate, acampamento e evento supõem um jogador | todo o `game/` | O `World` é passado explicitamente. Há N jogadores e a IA escolhe o alvo entre eles. |
| 6 | A UI pausa a simulação (`G.uiOpen`), e o painel de morte/cinemática desliga a IA (`G.uiOpen === 'death'`) | `main.js`, `enemies.js canTarget` | O servidor nunca pausa por causa de UI. Só o host local pausa o tick, e só com uma sessão conectada. |
| 7 | Interações são closures (`run: () => …`) e os painéis chamam funções de sim (`panels.js` importa `sell/buy/craft`) | `interact.js`, `panels.js` | Tudo vira dado: `InteractableId` + `Request` tipado, validado no servidor. |
| 8 | Aleatoriedade não determinística: `Math.random()` na perda Full Loot, no bônus de coleta, na estrela seguinte, na `gorgeActive`, em `rand/pick` | `zones.js`, `interact.js`, `stars.js`, `world.js`, `state.js` | `Rng` com semente por mundo. Mesmos comandos = mesmo estado (replays e testes). |
| 9 | A simulação depende de assets de render: `dodgeClip` consulta `P.model.actions`, a IA dorme quando `root.visible=false`, a ordem das estrelas vem de `G.sky.order` (`sky.js`) | `player.js`, `enemies.js`, `stars.js` | A sim decide `DodgeDir`/`HitSide`/`DeathFall` (enums). O cliente escolhe o clipe. A IA dorme por distância a jogadores. A ordem das estrelas é gerada na sim com semente. |
| 10 | Colisores de jogo são criados por código visual: árvores (`world-instances.js addCircle`), `extra-spots.js`, `world.js place()` | `world/*` | Esses colisores passam a ser pré-calculados offline (sim-pack) ou gerados por layout compartilhado em `core`. |
| 11 | O relevo final depende de um processamento no load (`carveRiver` em `heightfield.js`) | `world/heightfield.js`, `river.js` | O heightfield já escavado é gerado offline. Servidor e cliente leem o mesmo binário. |
| 12 | O save mistura personagem e mundo (EVT, CAMP, SKY, MKT) num único `localStorage` | `save.js` | Ficam dois registros: `CharacterRecord` (da conta) e `WorldRecord` (do servidor). |
| 13 | Os textos em português são gerados na simulação (Livro, avisos) | `book.js`, `economy.js`, … | A sim emite `MessageId`/`BookTemplateId` + parâmetros. O cliente localiza (pt-BR hoje). |
| 14 | A Turbulenta é um deslocamento de coordenadas (`TURB_RUNTIME_OFFSET`, `isTurbulentSpace(x)`) | `coordinates.js`, todo o `game/` | Viram `MapId` e `MapInstance`. Cada incursão é uma instância própria, com a "entrada individual" do documento. |
| 15 | A regra de equipar (treino exigido) é conferida só na interface (`panels.js`, ação de equipar) | `panels.js` | `Equip` é um `Request`; o servidor confere o treino antes de equipar. |

---

## 2. Biblioteca da janela: **SDL3**

**Recomendação: SDL3 (≥ 3.2), com o módulo `SDL_GPU` para renderizar.** Não SFML, e não SDL2.

- **SFML (inclusive a 3.x)** tem gráficos 2D. O jogo é 3D completo: PBR, esqueletos com skinning,
  sombras, GTAO, bloom, SMAA, céu atmosférico e milhares de instâncias com LOD. Da SFML sobraria só a
  janela e o contexto OpenGL, e o áudio e a rede dela não servem para um servidor de RPG.
- **SDL2** está em manutenção. A SDL3 é a linha atual: input de mouse relativo (a mira com ponteiro
  travado de hoje), gamepads, high-DPI, streams de áudio e o `SDL_GPU`. O `SDL_GPU` é uma API moderna
  única sobre Vulkan, D3D12 e Metal, o que evita escrever três backends ou depender do OpenGL, que está
  descontinuado no macOS.
- **Separação:** a SDL3 fica só no cliente. O servidor e os testes não a incluem nem a linkam.
- **Bônus:** num app nativo não existe Ctrl+W fechando aba, então `engine/keyguard.js` simplesmente
  desaparece.

### Pilha de bibliotecas (via vcpkg)

| Necessidade | Biblioteca | Camada |
|---|---|---|
| Janela, input, áudio, GPU | SDL3 + SDL_GPU (shaders HLSL compilados offline com SDL_shadercross) | cliente |
| Matemática | glm | core |
| glTF/GLB | fastgltf + meshoptimizer (os GLBs usam `EXT_meshopt_compression`) | cliente |
| Texturas | libwebp (lê os `.webp` atuais sem mudar o pipeline) | cliente |
| UI do jogo | RmlUi (HTML/CSS-like, casa com `index.html` + `styles.css` atuais). O `RenderInterface` é nosso, sobre SDL_GPU. | cliente |
| Ferramentas de debug | Dear ImGui (backends SDL3 + SDL_GPU) | cliente |
| JSON (dados, saves) | nlohmann-json | core |
| Log | `std::format` + `core/Log` (sem dependência externa) | core |
| Testes | Catch2 v3 | tests |
| Rede (fase futura) | GameNetworkingSockets (mensagens confiáveis e não confiáveis, criptografia) | net |
| Build | CMake ≥ 3.28 + `CMakePresets.json` + vcpkg manifest | — |

A animação usa um amostrador próprio sobre os clipes glTF (são poucos clipes, e as camadas procedurais
de `characters.js` agem sobre os ossos depois da amostragem). As entidades da simulação ficam em pools
tipados com handles geracionais (`SlotMap`), que são o mapeamento direto dos objetos atuais e fáceis de
serializar e iterar de forma determinística.

---

## 3. Arquitetura

```
             ┌──────────────── apps/local (único alvo que linka os dois lados) ─────────────────┐
             │                                                                                  │
  rpg_client ── PlayerCommand / Request ──► rpg_net (ITransport: LocalTransport agora, GNS depois) ──► rpg_server ──► rpg_sim
   (SDL3,      ◄── Snapshot / GameEvent ───                                                          (host, sessões,  (mundo
   render, UI)                                                                                        persistência)    autoritativo)
        │                                                                                                               │
        └───────────────────────────────────────────► rpg_core ◄──────────────────────────────────────────────────────┘
                   (dados do jogo, mundo estático, terreno/colisão, motor de movimento, protocolo, Rng, math)
```

### Regras de camada (verificadas no build)

1. `rpg_core` e `rpg_sim` não incluem SDL, GPU, áudio, RmlUi nem ImGui. O CMake só linka
   `rpg_sim → rpg_core`, e o script `tools/check_layering.py`, rodado no CI, falha se `src/sim` ou
   `src/core` incluírem `client/`, `SDL*` ou `Rml*`.
2. `rpg_client` **não linka** `rpg_sim`. Ele só conhece `rpg_core` (protocolo e mundo estático) e `rpg_net`.
3. A sim não conhece câmera, teclado ou tela. Ela recebe `PlayerCommand` e `Request` e produz estado e
   `GameEvent`.
4. A sim não gera texto de UI. Ela emite ids + parâmetros e a localização fica no cliente.
5. A sim roda com tick fixo (30 Hz, o mesmo `dt = 1/30` que os testes atuais usam), usa só o próprio
   `Rng` e nunca lê o relógio real.
6. Tudo o que cruza a fronteira é serializável. O `LocalTransport` serializa para bytes mesmo em
   processo, então o dia do UDP não muda nada acima da camada de rede.
7. O cliente nunca muta estado autoritativo. Ele só prevê o movimento do próprio jogador, com o mesmo
   `CharacterMotor` de `core`, e reconcilia com o snapshot.
8. Movimento, colisão e terreno ficam em `core` justamente para servir às duas pontas: autoridade no
   servidor e predição no cliente. Combate, IA, economia e perdas ficam só em `sim`.

### Fluxo de um quadro (modo local)

Cliente: coleta input → `CommandBuilder` monta `PlayerCommand` (seq, tick) → envia → recebe snapshots e
eventos → `SnapshotInterpolator` (atraso de ~2 ticks) → predição do jogador local → câmera → render → UI.

Servidor (thread própria): acumula tempo → para cada tick aplica os comandos por sessão → `World::step`
(mesma ordem do `tick()` de `main.js`: jogadores → montarias → inimigos → grupos → projéteis → acampamento
→ evento → estrelas → Turbulenta → mercados → cargas → NPCs → descobertas) → `SnapshotBuilder` por
sessão → `EventRouter`.

---

## 4. Árvore de arquivos

Tudo novo fica em `cpp/` na raiz do repositório. A demo JS continua intacta como referência e fonte das
fixtures de paridade. Entre colchetes está a origem no JS.

```
cpp/
  CMakeLists.txt                 alvos rpg_core, rpg_net, rpg_sim, rpg_server, rpg_client, apps, testes
  CMakePresets.json              debug/release/asan; windows-msvc, linux-clang
  vcpkg.json                     sdl3, glm, fastgltf, meshoptimizer, libwebp, rmlui, imgui, nlohmann-json, catch2
  cmake/Warnings.cmake, cmake/Sanitizers.cmake, cmake/Shaders.cmake (compila HLSL → SPIR-V/DXIL/MSL)
  data/                          dados de design, compartilhados pelo servidor e pelo cliente   [config.js]
    items.json weapons.json skills.json enemies.json zones.json markets.json recipes.json
    trainings.json origins.json starts.json balance.json constellations.json travelers.json
    discoveries.json encounters.json
  data/text/pt-BR/               textos só do cliente: nomes, descrições, avisos, modelos do Livro
    items.json skills.json messages.json book.json places.json ui.json
  shaders/                       *.hlsl: pbr, character (skinning), foliage, terrain, water, sky,
                                 shadow, gtao, bloom, smaa, tonemap, impostor, ui
  ui/                            *.rml / *.rcss (RmlUi)                          [index.html, styles.css]
  src/
    core/ …  net/ …  sim/ …  server/ …  client/ …  apps/ …
  tests/ …
```

### 4.1 `src/core/` → biblioteca `rpg_core` (compartilhada, sem dependências de plataforma)

**Básico**
- `core/Types.h`: aliases (`Vec2`, `Vec3` via glm, `Tick`, `Seconds`).
- `core/Math.h`: `clamp`, `lerp`, `smooth`, `angleDiff`, `lerpAngle`, `distXZ` (o `dist2` do JS), `expSmooth(rate, dt)`.   [state.js, `k()` de player.js]
- `core/Rng.h/.cpp`: PCG32 com semente; `range`, `rangeInt`, `pick`, `chance`. Também `Mulberry32` para paridade com `world.js rng(1337)` e `noise.js`.
- `core/Noise.h/.cpp`: `vnoise`, `fbm2`, `hash2`, `smoothstep`.   [world/noise.js]
- `core/Handle.h`, `core/SlotMap.h`: `EntityId` geracional e pool tipado.
- `core/FixedTimestep.h/.cpp`: acumulador de tick fixo.
- `core/GameClock.h/.cpp`: dia e hora a partir do tempo de jogo (`DAY_LENGTH`, `START_HOUR`).   [state.js `clock()`]
- `core/Log.h/.cpp`, `core/Assert.h`, `core/BinaryReader.h/.cpp` (little-endian, u16).

**Dados de design (só os campos de regra; nomes e descrições ficam no cliente)**
- `core/data/Ids.h`: `ItemId`, `SkillId`, `WeaponFamilyId`, `EnemyTypeId`, `ZoneId`, `MarketId`, `RecipeId`, `TrainingId`, … (índices internados no load).
- `core/data/DefTable.h`, `core/data/DataError.h`: tabela chave ↔ id na ordem de declaração; erro com arquivo e chave.
- `core/data/Defs.h`: todas as definições de regra.
  - `ItemDef`: peso, tipo, família, requisito, vida/velocidade da armadura, capacidade.   [ITEMS, GEAR_KINDS]
  - `WeaponDef`: ataque básico (tipo, dano, alcance, arco, windup, recover, escola) e as 2 técnicas.   [WEAPONS]
  - `SkillDef`: custo e cooldown. Na fase 2 ganha as fases (telegrafia, instante do golpe, fim, raio, dano, efeitos), e os números hoje fixos no `switch` de `updateSkill` passam para `skills.json`.   [SKILLS + player.js]
  - `EnemyDef`: [ENEMIES + guarda embutido em `spawnEnemy`].
  - `ZoneDef`: [ZONES, só a regra de perda].
  - `MarketDef`, `RecipeDef`, `TrainingDef`, `OriginDef`, `StartDef`: [MARKETS, MOUNT_PRICE, RESTART_KIT_PRICE, RECIPES, TRAININGS, ORIGINS, STARTS].
  - `Balance`: WALK, RUN, CROUCH, GRAVITY, JUMP, BOOST, AIR_CONTROL, MAX_SLOPE, ROUTE_SLOPE, `BASE_CAP`, `OVER_LIMIT`. Os números embutidos no meio do código (regeneração, esquiva…) entram junto com o `CharacterMotor`.   [player.js, collide.js, inventory.js]
- `core/data/GameData.h/.cpp`: carrega tudo e valida as referências cruzadas (receita → item, arma → técnica, …).

**Mundo estático (somente leitura; o servidor usa para autoridade e o cliente para predição, câmera e mira)**
- `core/world/MapId.h`: `MapKind { Region, Turbulent }` e `MapInstanceId`. Substitui o teste `isTurbulentSpace(x)`: as regras usam o mapa da entidade.
  - Cada mapa mantém o referencial da demo (a Turbulenta centrada em x = 1.400). Subtrair o deslocamento mudaria o arredondamento no último bit e quebraria a paridade exata.
  - Várias incursões são instâncias do mesmo mapa estático.
- `core/world/Coordinates.h`: `PLAN_Z_SIGN` e conversões plano ↔ cena.   [world/coordinates.js]
- `core/world/WorldManifest.h/.cpp`: limites, âncoras, rotas, placements e checagem de `runtime_schema`.   [world/runtime-manifest.js]
- `core/world/SimPack.h/.cpp`: lê o pacote pré-calculado (heightfield escavado, máscara, rio, colisores, pontes, campo de rotas).   [novo]
- `core/world/Heightfield.h/.cpp`: bilinear com a mesma diagonal da malha; `biomeAt`, `isWaterCell`, `slopeAt`.   [world/heightfield.js]
- `core/world/River.h/.cpp`: tabelas de nível, centro e meia-largura do rio (`waterLevelAt`, `riverCenterAt`, `wetnessAt`).   [heightfield.js/river.js, parte de consulta]
- `core/world/Terrain.h/.cpp`: `groundHeight` (terreno + tabuleiro e rampas das pontes) e `waterAt`.   [engine/terrain.js]
- `core/world/CollisionGrid.h/.cpp`: grade de 12 m com círculos/caixas; `resolve`, `testPoint`, `segmentBlocked`.   [game/collide.js]
- `core/world/RouteField.h/.cpp`: polilinhas, `routeDistance`, `distToPolyline`, `routeXAtZ`.   [world/world-routes.js, parte de dados]
- `core/world/ZoneMap.h/.cpp`: `zoneAt`, `placeAt` (retorna `PlaceId`), `LOC`, pontos protegidos/nunca protegidos, elipse dos Ermos.   [game/layout.js]
- `core/world/GameplayLayout.h/.cpp`: posições determinísticas dos objetos de jogo (serviços, bancadas, quadros, nós de coleta, postos de guarda, encontros, `PORTAL_SPOTS`, santuários/saídas da Turbulenta) e os colisores deles. O servidor usa os colisores e interagíveis, o cliente os modelos.   [game/world.js, layout.js, turbulent.js, parte de posição]
- `core/world/StaticWorld.h/.cpp`: agrega terreno, colisão, rotas, zonas e layout por `MapKind`; `StaticWorld::load(dir)`.

**Movimento compartilhado (autoridade no servidor, predição no cliente)**
- `core/movement/MoveEntity.h/.cpp`: limite de inclinação (maior na estrada), saída da água, `resolve`, limites do mapa.   [collide.js `moveEntity`]
- `core/movement/MotorState.h`: posição, yaw, `air{y, vy, vx, vz, boosted, t}`, `landT`, `crouchW`, dodge em andamento.
- `core/movement/CharacterMotor.h/.cpp`: `step(MotorState&, const MotorInput&, const StaticWorld&, dt)`. Cobre velocidade (andar, correr, agachar, armadura, mira, carga, vau, montado, fera), pulo e impulso com gravidade, controle aéreo, deslocamento da esquiva e altura final no chão/água.   [player.js, partes de movimento]

**Regras puras de animação usadas pela sim**
- `core/anim/AnimRules.h/.cpp`: `toLocal`, `hitSide`, `deathFall`, `dodgeDirection` (retorna `DodgeDir`, sem olhar clipes).   [engine/anim-select.js, parte usada pelo combate]

**Protocolo (o que cruza a rede)**
- `core/protocol/ProtocolVersion.h`
- `core/protocol/ByteStream.h/.cpp`: `Writer`/`Reader` com quantização de posição, ângulo e fração.
- `core/protocol/PlayerCommand.h/.cpp`: `seq`, `clientTick`, direção de movimento no mundo, botões (`Jump`, `Boost`, `Crouch`, `Run`, `Dodge`, `Attack`, `Aim`, `SkillQ`, `SkillE`, `Potion`), `aimYaw`, `aimPitch`, `aimPoint`, `aimEntity`, `clickTarget` (atacar ou usar).
- `core/protocol/Requests.h/.cpp`: ações discretas confiáveis: `CreateCharacter`, `Interact`, `Buy`, `Sell`, `Craft`, `Learn`, `Repair`, `BuyMount`, `RestartKit`, `Deliver`, `ShareReport`, `TakeLoot`, `DropItem`, `Equip`, `Unequip`, `Store`, `Withdraw`, `MountAction`, `EnterPortal`, `Respawn`, `ObserveSky`, `GiveInstrument`, `BookSetPublic`, `BookNote`, `ShowTitle`, `Pause` (só local).
- `core/protocol/Snapshot.h/.cpp`: `EntityState` (id, arquétipo, mapa, pos, yaw, `AnimState`, fração de vida, flags), `PrivatePlayerState` (vida, vigor, escudo, cooldowns, canalização, inventário, moedas, equipamento, carga) e `WorldState` (relógio, evento, acampamento, estrelas sumidas, portal/colapso). Delta por baseline confirmada é da fase online.
- `core/protocol/AnimState.h`: `LifeState`, `ActionKind` (swing/bow/crossbow/cast/rapid/heavy/thrust/slash/spin/charge), progresso da ação, `DodgeDir`, `HitSide` + tempo, `DeathFall`, `aimPitch`, agachado, fase aérea, montado, metamorfose.
- `core/protocol/GameEvents.h/.cpp`: `std::variant` com `Damage{alvo, valor, crítico, esquiva, bloqueio}`, `Heal`, `Status`, `Telegraph{forma, pos, raio, arco, comprimento, largura, direção, duração, estilo}`, `TelegraphCancel`, `ShotFired`, `Impact`, `Sound{SoundId, pos}`, `Message{MessageId, args, severidade}`, `BookEntry`, `ZoneChanged`, `PlaceChanged`, `PlayerDown`, `PlayerDied{DeathReport}`, `Respawned`, `HitMarker`, `StarVanished`, `ObservationResult`, `EventCompleted`, `TurbulentEntered`, `TurbulentLeft`, `ServiceOpened{kind}`.
- `core/protocol/MessageIds.h`, `core/protocol/SoundIds.h`: enums estáveis. Os textos ficam em `data/text`.

### 4.2 `src/net/` → biblioteca `rpg_net`

- `net/Transport.h`: interface `ITransport` (`send(conn, channel, bytes)`, `poll()`, eventos de conexão).
- `net/Channels.h`: `Commands` (não confiável, sequenciado), `Requests` e `Events` (confiáveis, ordenados), `Snapshots` (não confiável).
- `net/Packet.h/.cpp`: cabeçalho (versão do protocolo, canal, sequência).
- `net/LocalTransport.h/.cpp`: par de filas thread-safe em processo, com latência, jitter e perda simulados opcionais para testar a rede já em modo local.
- *(fase online)* `net/GnsTransport.h/.cpp`: GameNetworkingSockets.

### 4.3 `src/sim/` → biblioteca `rpg_sim` (autoritativa; só depende de `rpg_core`)

**Núcleo**
- `sim/World.h/.cpp`: dono dos mapas, pools, `Rng`, tempo, filas de eventos; `step(dt)` na ordem do `tick()`.   [main.js tick, state.js G]
- `sim/MapInstance.h/.cpp`: instância viva de um mapa (região ou Turbulenta #n) com a referência ao `StaticWorld` e o hash espacial dinâmico.
- `sim/SpatialHash.h/.cpp`: consultas por raio, setor e segmento, no lugar dos `for (const e of G.enemies)`.
- `sim/EventBus.h/.cpp`: eventos internos entre sistemas (substitui `on/emit`) e a fila de `GameEvent` de saída com destinatários (dono, próximos, todos no mapa).   [state.js]
- `sim/Stats.h/.cpp`: contadores por personagem (`stat`) e flags.   [state.js]

**Jogador**
- `sim/player/Player.h/.cpp`: dados do jogador (sem modelo).   [player.js `createPlayer`]
- `sim/player/PlayerController.h/.cpp`: aplica `PlayerCommand`, regenera vida/vigor/aura, cuida dos timers de escudo/fera e da máquina de estados (free/attack/skill/dodge/stagger/channel/down/dead), usando `CharacterMotor`; resgate por guarda; troca de zona/lugar.   [`updatePlayer`]
- `sim/player/PlayerActions.h/.cpp`: `basicAttack`, `resolveBasic`, `assistYaw`, pulo, esquiva, poção, `followCommand` (clique para atacar/usar).
- `sim/player/Channeling.h/.cpp`: canalização com `ChannelAction` tipado (Reparo, Coleta, Montar, Chamar montaria, Extrair, Fragmento), sem closures.   [`startChannel`/`cancelChannel`]

**Combate**
- `sim/combat/Damage.h/.cpp`: `hurtPlayer` (invulnerável, escudo, derrubar da montaria, desgaste), `hurtEnemy`, `killEnemy` (moedas, drops, carga), `markHit`, `inCombat(player)`.   [combat.js]
- `sim/combat/MeleeSweep.h/.cpp`: arco ou círculo com tolerância de altura.
- `sim/combat/Projectiles.h/.cpp`: flecha, virote e magia; balística, segmento percorrido, crítico na cabeça, perfuração, escolas. O projétil é entidade replicada.
- `sim/combat/StatusEffects.h/.cpp`: slow, root, burn (tick de 0,5 s), curse, stun, invuln, escudo, aura, metamorfose.
- `sim/combat/SkillSystem.h/.cpp`: executa as fases de `SkillDef` (telegrafia → golpe → fim) e registra os comportamentos especiais (`investida`, `puxaoFoice`, `drenoVital`, `formaFera`, `reparoCampo`, …) num `SkillBehaviorRegistry`, no lugar do `switch` de 36 casos.   [`useSkill`/`updateSkill`]

**IA**
- `sim/ai/Enemy.h/.cpp`: dados do inimigo, sem modelo.   [`spawnEnemy`]
- `sim/ai/HostileBrain.h/.cpp`: idle/patrulha/vaguear/saquear/perseguir/windup/recover/stun/retornar, `startWindup`, `strike`, `canTarget` escolhendo entre N jogadores, sono por distância.   [`updateHostile`]
- `sim/ai/GuardBrain.h/.cpp`: [`updateGuard`, `nearestHostile`].
- `sim/ai/Steering.h/.cpp`: `stepToward` (root/slow, bandido não entra em zona protegida) e `separate`.
- `sim/ai/SpawnGroups.h/.cpp`: `defineGroup` e `updateGroups`, que reaparecem sem jogador a menos de 55 m.

**Itens e economia**
- `sim/items/InventoryEntry.h`: `Stack{ItemId, qty}` e `Gear{ItemId, uid, cond}`.
- `sim/items/Inventory.h/.cpp`: `count`, `weightOf`, `addTo`, `removeFrom`, `receive` (bolsa → alforjes → sobrecarga), `capacity`, `loadState`, `haveAll`, `consume`, `wear`, `equippedList`.   [inventory.js]
- `sim/items/ItemUidAllocator.h/.cpp`: uid global do mundo (substitui `uidSeq` e `setUidSeq`).
- `sim/economy/Markets.h/.cpp`: demanda por mercado (estado **do mundo**), preços, `sell`, `buy`, `buyMount`, `restartKit`, recuperação da demanda.   [economy.js]
- `sim/economy/Crafting.h/.cpp`: `craftCheck`, `craft`, `learn`, `repairCost`, `repair`.   [economy.js]

**Sistemas do mundo**
- `sim/systems/DeathRules.h/.cpp`: matriz de perda por zona (Rng com semente para os 30% destruídos), `DeathReport`, `respawn`, abrigo.   [zones.js]
- `sim/systems/LootBags.h/.cpp`: carga no chão como entidade replicada, com dono, expiração e reserva por saqueador.   [zones.js `createLootBag`]
- `sim/systems/Interaction.h/.cpp`: resolve `Interact{id}` com checagem de distância e combate; serviços respondem `ServiceOpened`.   [interact.js `targetsNear`]
- `sim/systems/GatherNodes.h/.cpp`: cargas, rebrota e bônus de rendimento.   [world.js NODES, interact.js `gather`]
- `sim/systems/Mounts.h/.cpp`: montaria por jogador; chamar, montar, desmontar; alforjes voltam ao estábulo.   [mount.js]
- `sim/systems/RegionalEvent.h/.cpp`: contribuições, entregas, reconhecimento, outros participantes simulados, conclusão e efeitos da reabertura.   [event.js]
- `sim/systems/Camp.h/.cpp`: estados Estabelecido → Pressionado → Deslocado → Em recuperação, composição e regeneração.   [camp.js]
- `sim/systems/StarVanishing.h/.cpp`: ordem das estrelas gerada aqui com semente (sai de `sky.js`), sumiço, fases, `observe` → `ObservationResult`, instrumento.   [stars.js]
- `sim/systems/Turbulent.h/.cpp`: ciclo do portal, `MapInstance` por incursão, santuários, saídas, sombras, colapso e extração.   [turbulent.js]
- `sim/systems/Npcs.h/.cpp`: NPCs de serviço e viajantes percorrendo rotas.   [npcs.js]
- `sim/systems/Discovery.h/.cpp`: [discovery.js].
- `sim/systems/Book.h/.cpp`: o Livro por personagem (`first`, `tally`, `log`, `note`, `grantTitle`). As entradas guardam `BookTemplateId` + parâmetros.   [book.js]
- `sim/systems/WorldPopulation.h/.cpp`: coloca na instância o que o `GameplayLayout` descreve (interagíveis, NPCs, grupos, encontros, guarda do canteiro).   [world.js `buildWorld`, `initCanteiroGuard`]

**Registros persistentes**
- `sim/persist/CharacterRecord.h/.cpp`: perfil, posição, moedas, treinos, bolsa, alforjes, armazém, equipamento, bolsas no estábulo, Livro, stats, flags.
- `sim/persist/WorldRecord.h/.cpp`: tempo, evento, acampamento, céu, demanda dos mercados.
- `sim/persist/SaveMigration.h/.cpp`: `migrateSave`, `isValidPosition`, `safeSpawn`, importação do save JSON da demo.   [save.js]

### 4.4 `src/server/` → biblioteca `rpg_server` (host; depende de `rpg_sim` + `rpg_net`)

- `server/ServerHost.h/.cpp`: laço de tick fixo, que roda numa thread (local) ou no main (dedicado); aceita conexões e distribui mensagens.
- `server/ServerConfig.h/.cpp`: tick rate, caminhos de dados e saves, `allowPause` (só single-player).
- `server/Session.h/.cpp`: conexão ↔ jogador, buffer de comandos, último `seq` confirmado, conjunto de relevância.
- `server/CommandValidator.h/.cpp`: limita valores, taxa, velocidade e distância de mira/interação (a base anti-trapaça).
- `server/RequestHandler.h/.cpp`: despacha `Request`s para os sistemas da sim.
- `server/SnapshotBuilder.h/.cpp`: snapshot por sessão com gerência de interesse (raio e instância de mapa).
- `server/EventRouter.h/.cpp`: filtra `GameEvent`s por destinatário.
- `server/PersistenceStore.h`: `IPersistenceStore` (`load`/`save` de personagem e mundo).
- `server/JsonFileStore.h/.cpp`: saves em arquivo no diretório do usuário (substitui o `localStorage`); autosave a cada 20 s e no encerramento; não salva dentro da Turbulenta.
- `server/DevCommands.h/.cpp`: `tp`, `give`, `coins`, `hurt`, `portal`, `killNear`, `contribute`, `night`, `vanish`, `settle`, só em build de desenvolvimento.   [`window.__demo`]

### 4.5 `src/client/` → biblioteca `rpg_client` (**não** linka `rpg_sim`)

**Aplicação e rede**
- `client/ClientApp.h/.cpp`: laço principal e telas (carregando → título → criação → jogo); fases de boot com progresso.   [main.js]
- `client/ClientSession.h/.cpp`: handshake, recebe snapshots e eventos, envia comandos e requests.
- `client/ClientWorld.h/.cpp`: tabela de entidades replicadas por `EntityId`, estado privado do jogador, estado do mundo.
- `client/net/SnapshotInterpolator.h/.cpp`: buffer de snapshots e interpolação com atraso.
- `client/net/Prediction.h/.cpp`: predição do jogador local com `core::CharacterMotor` e reconciliação (pode ficar desligada no modo local).
- `client/CommandBuilder.h/.cpp`: WASD relativo à câmera, Shift/Ctrl/Espaço/C/Q/E/1, mira, clique → `PlayerCommand`.   [player.js, input em `updatePlayer`]
- `client/Settings.h/.cpp`: sensibilidade e inversão da câmera, qualidade, volume, em `settings.json`.   [localStorage]

**Plataforma (SDL3)**
- `client/platform/Window.h/.cpp`: janela, tela cheia, high-DPI.
- `client/platform/Input.h/.cpp`: teclas por scancode (independem do layout ABNT/US), mouse, modo relativo (mira), roda, arrasto; pressed/released por quadro.   [engine/input.js]
- `client/platform/InputBindings.h/.cpp`: ações ↔ teclas remapeáveis, acorde Tab+1/2/3, atalhos I/B/J/M/R/V/F/Esc.   [main.js `handleKeys`]
- `client/audio/AudioDevice.h/.cpp`: stream SDL3 + mixer.
- `client/audio/SfxSynth.h/.cpp`: sons sintetizados e ambiência.   [engine/audio.js]

**Jogo (apresentação e input)**
- `client/game/AimRaycaster.h/.cpp`: raio da câmera contra entidades replicadas e o terreno de `core`.   [`updateAim`]
- `client/game/Picking.h/.cpp`: o que está sob o cursor (inimigo > objeto) e o anel de hover.   [pointer.js]
- `client/game/ThirdPersonCamera.h/.cpp`: órbita, mira sobre o ombro (V), spring arm com `Terrain`/`CollisionGrid`, recuo, tremor, presets, reset ao norte, modo céu, câmera livre.   [`updateCamera`]
- `client/game/InteractPrompt.h/.cpp`: interagível mais próximo (F), a partir do layout + estado replicado.
- `client/game/EventPresenter.h/.cpp`: `GameEvent` → partículas, texto flutuante, som, aviso, hitmarker, recuo da câmera nos próprios disparos.

**Render (SDL_GPU)**
- `client/render/GpuDevice.h/.cpp`: device, swapchain, command buffers, upload.
- `client/render/ShaderLibrary.h/.cpp`: carrega os shaders compilados de `shaders/`.
- `client/render/Renderer.h/.cpp`: passes (sombra → opacos → céu → água → transparentes → GTAO em meia resolução → bloom com teto → tone map → SMAA → UI); não redesenha quando pausado.   [engine/renderer.js]
- `client/render/Camera.h/.cpp`, `client/render/Frustum.h/.cpp`
- `client/render/Mesh.h/.cpp`, `client/render/Texture.h/.cpp` (WebP, mips; mips de folhagem que preservam cobertura), `client/render/Material.h/.cpp` (PBR, personagem, folhagem, terreno, água).   [engine/materials.js]
- `client/render/ShadowMap.h/.cpp`: uma vez por quadro; só personagens próximos projetam.
- `client/render/PostProcess.h/.cpp`: GTAO, bloom, SMAA, saída.
- `client/render/Sky.h/.cpp`: céu atmosférico, sol, lua, estrelas, constelações, luz do dia, ambiente do HDRI do kit. Desenha as estrelas sumidas conforme o `WorldState`.   [engine/sky.js]
- `client/render/InstancedField.h/.cpp`: LOD por instância.   [engine/lod-field.js]
- `client/render/Impostors.h/.cpp`: [world/impostors.js].
- `client/render/Quality.h/.cpp`: Alta/Média/Baixa e ajuste automático.   [engine/quality.js]
- `client/render/DebugDraw.h/.cpp`

**Assets**
- `client/assets/GltfLoader.h/.cpp`: fastgltf + decodificação meshopt.   [runtime-loader.js, models.js]
- `client/assets/AssetManager.h/.cpp`: cache e carregamento assíncrono com relatório de fases.
- `client/assets/ProceduralTextures.h/.cpp`: [engine/textures.js], com cache em disco.
- `client/assets/NatureTextures.h/.cpp`: [engine/nature-textures.js].

**Vista do mundo**
- `client/world/TerrainRenderer.h/.cpp`: 16 blocos de 256 m, peças de 64 m com duas resoluções.   [world/scene-world.js]
- `client/world/WaterRenderer.h/.cpp`: superfície do rio.   [river.js, parte visual]
- `client/world/RouteRenderer.h/.cpp`: faixas das estradas.   [world-routes.js, parte visual]
- `client/world/SceneProps.h/.cpp`: props da região e da Turbulenta, substituições extras, tabuleiro das pontes.   [scene-world.js, extra-props.js, extra-seat.js, bridge-roadway.js, world-materials.js]
- `client/world/Vegetation.h/.cpp`: árvores, arbustos e rochas lidos da lista de instâncias pré-calculada; kit de natureza.   [world-instances.js, nature-kit.js]
- `client/world/GroundCover.h/.cpp`: grama/flores por célula em volta da câmera (só visual).   [ground-cover.js]
- `client/world/VegetationFields.h/.cpp`, `client/world/FunctionalAreas.h/.cpp`: campos que a cobertura do chão consulta.   [vegetation-fields.js, functional-areas.js]
- `client/world/GameplayPropsView.h/.cpp`: modelos de serviços, nós (esgotado ou não), santuários, pedras de extração, portal.   [world.js, turbulent.js, engine/props.js]
- `client/world/MapStreaming.h/.cpp`: mostra ou oculta cenas por mapa e distância.

**Personagens e animação**
- `client/anim/Skeleton.h/.cpp`, `client/anim/AnimationClip.h/.cpp`, `client/anim/AnimationMixer.h/.cpp` (amostragem glTF, crossfade sem estalo).
- `client/anim/ClipRegistry.h/.cpp`: clipes base, extras e o lote de combate.   [extra-clips.js, combat-clips.js]
- `client/anim/ClipSelect.h/.cpp`: `locomotionWeights`, `dodgeClip`, `hitClip`, `deathClip`.   [anim-select.js, parte de clipes]
- `client/anim/HumanoidAnimator.h/.cpp`: fase caminhada/corrida, inércia, inclinação, idle vivo, estabilização da cabeça, coluna na mira, arco puxado, agachar, pulo.   [characters.js `animateHumanoid`]
- `client/anim/BeastAnimator.h/.cpp`, `client/anim/MountAnimator.h/.cpp`: [characters.js].
- `client/entities/CharacterFactory.h/.cpp`: humanoide por raça e tom (guarda, bandido, sombra), fera, montaria, arma presa à mão.   [makeHumanoid/makeBeast/makeMount, enemies.js `buildModel`]
- `client/entities/EntityView.h/.cpp`: visual de uma entidade replicada; pulso de dano, cintilar no respawn, animação a cada 3/6 quadros quando longe.
- `client/entities/EntityViewRegistry.h/.cpp`: cria e remove views conforme o snapshot.
- `client/fx/Telegraphs.h/.cpp`, `client/fx/Particles.h/.cpp`, `client/fx/FloatingText.h/.cpp`, `client/fx/EntityBars.h/.cpp`, `client/fx/HoverRing.h/.cpp`, `client/fx/ProjectileView.h/.cpp`: [engine/fx.js, combat.js visual].

**UI (RmlUi) e localização**
- `client/ui/UiSystem.h/.cpp`, `client/ui/RmlRenderInterface.h/.cpp` (sobre SDL_GPU), `client/ui/RmlSystemInterface.h/.cpp`.
- `client/ui/Localization.h/.cpp`: `MessageId`/`BookTemplateId`/ids de item → texto pt-BR.
- `client/ui/Screens.h/.cpp`: carregamento com fases e regras das zonas, título, criação de personagem.   [index.html, main.js]
- `client/ui/Hud.h/.cpp`: vitais, faixa de zona, carga, barra de ações, avisos, prompt.   [hud.js]
- `client/ui/MapView.h/.cpp`: relevo pré-renderizado, minimapa e mapa completo.   [map.js]
- `client/ui/Menu.h/.cpp`: abas Inventário, Livro, Mapa e Intenções.   [menu.js]
- `client/ui/BookUi.h/.cpp`: [bookui.js].
- `client/ui/panels/`: `InventoryPanel`, `MarketPanel`, `ForgePanel`, `TrainerPanel`, `StoragePanel`, `StablePanel`, `BoardPanel`, `CanteiroPanel`, `FourQuestionsPanel`, `AstronomerPanel`, `TravelerPanel`, `LootPanel`, `PortalPanel`, `DeathPanel`, `IntentsPanel`, `PausePanel` (`.h/.cpp` cada). Cada ação vira um `Request`.   [panels.js]
- `client/ui/DebugTools.h/.cpp`: ImGui (câmera livre, estado, vegetação, qualidade, comandos de dev via `Request`).

### 4.6 `src/apps/`

- `apps/local/main.cpp`: o jogo de hoje. Sobe o `ServerHost` numa thread e o `ClientApp` ligados por `LocalTransport`. **É o único alvo que linka cliente e servidor.**
- `apps/server/main.cpp`: servidor dedicado headless. Já é útil agora para testes de longa duração com bots de comandos.

### 4.7 `tests/` → `rpg_tests` (Catch2)

- `tests/core/`: `HeightfieldTests`, `TerrainTests` (pontes e rampas), `CollisionTests`, `MoveEntityTests`, `CharacterMotorTests`, `ZoneMapTests`, `ProtocolRoundTripTests`, `RngTests`.
- `tests/sim/`: `CombatTests`, `SkillTests`, `EnemyAiTests`, `InventoryTests`, `EconomyTests`, `DeathRulesTests`, `EventCampTests`, `TurbulentTests`, `SaveMigrationTests`, `DeterminismTests` (mesma semente + mesmos comandos → mesmo hash de estado).
- `tests/client/ClipSelectTests.cpp`: [demo/tests/animation.test.js].
- `tests/parity/`: compara o C++ com as fixtures geradas a partir do JS (`groundHeight`, `zoneAt`, `placeAt`, `routeDistance`, colisores, preços, matriz de perda).   [demo/tests/world.test.js]
- `tests/arch/`: o CMake confirma que `rpg_sim`/`rpg_core` não dependem de SDL e roda `tools/check_layering.py`.

---

## 5. Pipeline offline (reaproveita o JS existente)

Scripts Node em `demo/tools/`, empacotados com o esbuild que `build.mjs` já usa, para importar os módulos
atuais sem reescrevê-los:

- `demo/tools/export-game-data.mjs`: `config.js` → `cpp/data/*.json` (regras) e `cpp/data/text/pt-BR/*.json`
  (textos). A partir daí, o JSON é a fonte única.
- `demo/tools/bake-sim-world.mjs` (feito na Fase 1a):
  - **Carga:** roda no Node a sequência de boot da demo inteira (texturas → modelos → mundo com o kit de natureza → `buildWorld` → `initTurbulent`), com o ambiente mínimo de navegador de `tools/lib/node-env.mjs`.
  - **Captura:** um plugin do esbuild acrescenta, só nesse pacote, exportações de estruturas internas (grade de colisores, máscara, campo de rotas, pontos de zona). `demo/src` não muda.
  - **Saída em `cpp/assets/sim`:**
    - relevo escavado, máscara, rio, campo de rotas e pontes;
    - 24.753 colisores na ordem de inserção;
    - lugares e zonas.
  - **Fixtures:** na mesma carga, grava `cpp/tests/parity/fixtures/world-parity.json` com terreno, colisão, trajetos de `moveEntity` e ruído. Isso substitui o `export-parity-fixtures.mjs` previsto.
  - O conjunto de colisores é determinístico e não depende da qualidade gráfica (conferido por hash).
  - A lista de instâncias de vegetação para o cliente desenhar entra no mesmo script na Fase 4.

Os assets visuais (GLB, WebP, `world-manifest.json`) são lidos como estão.

---

## 6. Fases de implementação

| Fase | Entrega | Critério de pronto |
|---|---|---|
| 0 | Esqueleto CMake/vcpkg, alvos vazios, `check_layering.py`, `export-game-data.mjs` | build limpo em Linux e Windows; checagem de camadas no CI |
| 1 | `core` (dados, mundo estático, movimento, protocolo) + `bake-sim-world.mjs` + fixtures | testes de paridade com o JS passando |
| 2 | `sim` + `server` headless: jogador, combate, técnicas, IA, grupos, inventário, economia, perdas, cargas; `apps/server` | cenários por comandos roteirizados; determinismo |
| 3 | Cliente grey-box: SDL3 + SDL_GPU, terreno, cápsulas no lugar das entidades, câmera, input → `LocalTransport` → servidor | andar, lutar e morrer contra a sim real em `apps/local` |
| 4 | Assets: personagens glTF + animação, props, vegetação com LOD, céu | paridade visual básica com a demo |
| 5 | UI RmlUi: HUD, menu, painéis via `Request`, Livro, mapas, telas | loop completo de comércio, fabricação e treino |
| 6 | Evento, acampamento, estrelas, Turbulenta (instâncias), NPCs, montaria, descobertas, persistência em arquivo + importação do save da demo | todo o recorte do capítulo 16 jogável |
| 7 | Pós-processamento, sombras, impostores, cobertura do chão, qualidade automática | desempenho ≥ demo nos mesmos cenários de `measure-benchmark.mjs` |
| 8 (online) | `GnsTransport`, predição e reconciliação ligadas, delta de snapshots, interesse por raio, contas/banco | 2+ clientes remotos no mesmo mundo |

---

## 7. Verificação

- **Paridade:** `ctest -R parity` compara terreno, zonas, rotas, colisão, preços e regras de perda com as
  fixtures exportadas da demo JS (mesmas entradas, tolerância de float).
- **Determinismo:** `DeterminismTests` roda N ticks com semente e comandos fixos duas vezes e compara o
  hash do `World`.
- **Separação:** `tools/check_layering.py`, mais `ldd`/`dumpbin` de `rpg_server` sem SDL, no CI.
- **Rede simulada:** `apps/local --net-sim=latency:120,jitter:30,loss:2` tem que continuar jogável, o que
  prova que nada depende de estar no mesmo processo.
- **Fim a fim:** `apps/local` percorre o roteiro da demo (criar → mercado → coleta → bancada → Passagem →
  derrota na Fronteira → recuperar carga → portal → extração), no modo manual e em teste roteirizado
  pelos `DevCommands`.

---

## 8. Como a simulação foi implementada (fases 1b e 2)

### Estrutura

- **`rpg_core`** ganhou o que servidor e cliente compartilham:
  - `JsMath`: `sin`, `cos`, `atan2` e `exp` do fdlibm, na forma do V8. A libm do sistema diverge do
    V8 no último bit em 2–19% dos argumentos; com isso a simulação ficava igual à demo por poucos
    segundos. Além da paridade, dá o mesmo resultado em Linux, Windows e macOS (predição do cliente).
  - `Random`: `IRandom` injetável (`Pcg32Random` no jogo, `Mulberry32Random` nos testes de paridade).
  - `movement/CharacterMotor`: velocidade do passo, pulo, controle aéreo, gravidade e altura no vau.
  - `anim/AnimRules`: lado do golpe, lado da queda, clipe da esquiva, pesos de locomoção e pose das
    técnicas.
  - `world/GameplayLayout` (serviços, coleta, NPCs, grupos, portais, Turbulenta, descobertas) e
    `world/SkyLayout` (as 1.740 estrelas e a ordem de desaparecimento).
  - `protocol/`: `ByteStream` (um `io()` por mensagem serve para ler e escrever), `PlayerCommand`,
    `Requests`, `GameEvents`, `Snapshot`, `Session` e o catálogo de mensagens e sons.
- **`rpg_sim`**: o estado (`Sim`, o `G` da demo) e os sistemas como funções livres, um arquivo por
  módulo da demo: `Player`, `Combat`, `Enemies`, `Zones`, `Mount`, `Economy`, `Interact` e
  `WorldSystems` (acampamento, evento, céu, Turbulenta, NPCs, descobertas). `World` é a interface:
  jogadores, comandos, pedidos, passo, snapshot e eventos.
- **`rpg_server`**: `ServerHost` com sessões (Hello → Welcome), comandos, pedidos, snapshots por
  sessão com raio de interesse, eventos por destinatário e bots (`rpg_server --bots N`).

### Um jogador na demo, N aqui

O que era do único `G.player` passou para o `Player`: montaria, Livro, estatísticas, marcas,
contribuição ao evento, observações do céu e a incursão na Turbulenta, que é uma instância própria do
mapa. A IA escolhe o jogador mais próximo que pode atacar, no mesmo mapa e instância. Com um
jogador, tudo se comporta exatamente como na demo.

### Paridade por roteiro

`demo/tools/bake-sim-world.mjs` roda, no Node, as funções de jogo da própria demo (updatePlayer,
updateEnemies, combate, economia, acampamento, evento, céu, Turbulenta) com teclas e mira
roteirizadas e `Math.random` trocado por um mulberry com semente. São 19 roteiros
(`demo/tools/bake/scenarios.mjs`), do boot completo (todos os inimigos, NPCs e sistemas) a cada
técnica das 18 famílias de armas, derrotas por zona, saque, resgate, mercado e fabricação,
acampamento, estrelas, coleta e Turbulenta. `tests/parity/SimParityTests.cpp` repete cada roteiro e
compara, quadro a quadro, o hash dos bits de tudo o que se move (jogador, inimigos, projéteis) e, no
fim, inventários, moedas, estatísticas, Livro, acampamento, evento, céu, cargas, demanda e pontos de
coleta. **A igualdade é exata.**

Para isso o harness separa a aleatoriedade visual da demo (partículas, tremor e recuo de câmera,
olhares dos personagens e os UUIDs que o three.js sorteia a cada objeto): ela passa a usar outro
gerador, porque no C++ é do cliente.

### Desvios da demo

A regra é reproduzir a demo, inclusive o que parece estranho. As exceções:

| O que a demo faz | O que o C++ faz | Por quê |
|---|---|---|
| `turbulent.js` usa `ITEMS` sem importá-lo: o tick lança ReferenceError quando uma figura persegue o jogador na Turbulenta | registra a arma da figura, como o código pretendia | a demo trava nesse ponto; o bake injeta o import só no pacote dele |
| no colapso da Turbulenta, repõe `stateT = 4,4` a cada passo: o jogador fica derrubado para sempre | derruba uma vez e a derrota acontece | a região não teria saída |
| nenhuma validação nos painéis | o servidor confere distância do serviço, moedas, capacidade e combate | anti-trapaça |

Mantidos de propósito: o clique de ataque com magia nunca entra em alcance (a conta da demo dá NaN);
o `reparoCampo` e a queimadura que dão crédito a quem causou; a ordem dos arrays e dos sorteios.

## 9. O cliente (fase 3)

### Duas bibliotecas

- `rpg_client_core`: tudo o que não precisa de plataforma — `ClientSession` (Hello/Welcome, comandos,
  pedidos, eventos), `SnapshotInterpolator` (atraso de 1,5 passo, ângulos pelo lado curto, teleporte
  sem deslizar), `ClientWorld`, `ThirdPersonCamera` (porte linha a linha de `updateCamera`: órbita,
  ombro, spring arm, recuo, tremor, volta ao norte), mira (`updateAim`), alvos e clique
  (`targetsNear`, `pick`), `CommandBuilder`, `FxState` (eventos → textos flutuantes, telegrafias,
  partículas, avisos, relatório de derrota), `Localization` (`data/text/pt-BR`), atlas FreeType,
  HUD e telas em desenho imediato, malhas do relevo e do cenário, PNG. Testado sem janela.
- `rpg_client`: `Platform` (SDL3: janela, teclas por scancode, mouse relativo na mira, texto),
  `Renderer` (SDL_GPU) e `ClientApp`. Só estes diretórios podem incluir a SDL (`check_layering.py`).

### Render (SDL_GPU, Vulkan, SPIR-V)

Shaders em GLSL (`shaders/src`), compilados para SPIR-V no build (glslangValidator) e embutidos no
executável; os `.spv` também ficam versionados em `shaders/spv`. Por quadro: cena em HDR (céu →
relevo em blocos de 64 m com duas resoluções e recorte por frustum → cenário e entidades
instanciados → água → efeitos sem luz) e saída RGBA8 (ACES filmic como o three.js + sRGB, depois a
interface). A saída é copiada para a janela e, sob `--screenshot`, baixada para PNG. Sem janela
(`--headless`), o SDL usa o driver de vídeo `offscreen` e tudo roda igual: é o que o CI faz com o
Vulkan por software do mesa (lavapipe).

A luz segue `applyDaylight` (cores lineares, sol/lua, hemisfério, névoa exponencial, intensidade do
ambiente). As cores do chão aproximam as camadas de solo de `scene-world.js`; os modelos, texturas e
a vegetação de verdade entram na fase 4.

### Desvios da demo no cliente

| Demo | Cliente C++ | Por quê |
|---|---|---|
| a UI pausa a simulação (`G.uiOpen`) | o painel manda `kHeldUiOpen` no comando; só o jogo local pausa (`ReqPause`) | servidor autoritativo |
| o cursor sai da mira pelo Pointer Lock do navegador (e o Esc solta) | modo relativo da SDL enquanto o botão direito está apertado | mesmo efeito, sem o navegador |
| Ctrl+W fecha a aba (keyguard.js) | não existe | app nativo |

## 10. O cliente com o pacote visual da demo (fase 4)

### Pacote visual em vez de carregadores próprios

Em vez de portar as ~4.000 linhas que montam a cena (`world/*`, `engine/props.js`,
`engine/textures.js`, `engine/models.js`), `demo/tools/bake-client-scene.mjs` roda essa montagem no
Chromium e grava `cpp/assets/client`: geometrias, materiais com os recursos de `patchMaterial`,
texturas, relevo (só os pesos das camadas; a malha sai do heightfield e confere por hash), cenário,
instâncias, campos de LOD, modelos de jogo e os personagens prontos (pose de repouso, pele, clipes já
adaptados a cada modelo, empunhaduras). O C++ não lê GLB nem gera texturas procedurais.

### Render

- `world/PackScene`: um buffer de vértices para tudo; `WorldView` porta `updateWorldDetail` e
  `updateLODFields` (nível por instância com histerese de 3 m).
- `surface.frag`/`terrain.frag`/`pbr.glsl`: o `MeshStandardMaterial` do three r185 (GGX
  multiespalhamento com a tabela DFG copiada do three), Lambert e Basic, os recursos tri, occ, splat,
  wind, grass, water e shore, e névoa exponencial.
- Céu: Preetham com nuvens, a cúpula da Turbulenta, estrelas, constelações, lua e o anel do portal.
- Personagens (`client/anim`, `client/entities`): `Rig` (nós, pele, clipes), `Mixer` (o
  `AnimationMixer` do three: ações na ordem de ativação, LoopRepeat, acúmulo por peso e o resto até 1
  completado com a pose original), `HumanoidAnimator` e `QuadrupedAnimator` (portes linha a linha de
  `animateHumanoid`, `postura`, `animateBeast`, `animateHound`, `animateMount`), `CharacterViews`
  (uma vista por entidade replicada: monta o `s` de player.js, enemies.js, npcs.js e mount.js,
  escolhe molde, tom e arma) e `skinned.vert` (paleta de ossos num storage buffer por quadro, normais
  como o `skinnormal_vertex` do three).

### Paridade

`tests/parity/fixtures/anim-parity.json` sai do mesmo bake: roteiros de entrada (parado com e sem
arma, andar e correr, de lado e para trás, golpes, arco, esquiva, rolamento, impacto, morte, derrubado,
agachar e pular, montado, canalização, metamorfose, fabricação) rodados nos animadores da demo com
`Math.random` fixo. `AnimTests` refaz cada roteiro nos três humanoides e nos três quadrúpedes e exige a
mesma pose em cada nó (até 2·10⁻³ rad), o mesmo corpo e os mesmos vértices com pele no mundo (até
2 mm).

### Desvios e particularidades

| Demo | Cliente C++ | Por quê |
|---|---|---|
| GLB carregados e preparados na hora (`models.js`) | o bake grava o resultado; o plano previa fastgltf | o que a demo calcula na carga já vem pronto, e igual |
| texturas procedurais em canvas, PNG/JPEG embutidos | WebP q95 com alfa sem perdas (imagens embutidas: os bytes originais) | ~20 MB em vez de ~54 MB; `--lossless` grava sem perdas |
| PMREM do three para o reflexo do ambiente | harmônicos esféricos (difusa) + equirretangular com mip por rugosidade | aproximação mais simples do PMREM: a difusa é a mesma, o reflexo fica um pouco diferente |
| o equirretangular do IBL sobe com `flipY = false` (de cabeça para baixo) | reproduzido | paridade |
| `P.aimT` nunca sai de 0 (a pose de mira com arco não aparece) | reproduzido (`aim = 0`) | paridade |
| `quadruped()` guarda só a rotação da pose base, e o primeiro `mixerStep` zera a posição de Spine1, Spine2, Head e Jaw da leoa | reproduzido | paridade (a leoa só aparece sem o modelo do cão do vazio) |
| sombras, GTAO, bloom e cobertura do chão | fase 7 | |
