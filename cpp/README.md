# Projeto Game — C++20

Reescrita da demo (`../demo`, JavaScript + three.js) em C++20. A simulação fica separada do render e
do input desde o início, para o jogo virar RPG online sem reescrita. A arquitetura, as camadas e o
plano de fases estão em [ARQUITETURA.md](ARQUITETURA.md).

## Estado

**Fases 0 a 3 concluídas:** o jogo inteiro da demo roda no servidor, e o cliente (SDL3 + SDL_GPU/Vulkan)
joga contra ele: andar, correr, pular, mirar, lutar, coletar, morrer e reaparecer.

**Fase 4 concluída:** o mundo é o da demo — relevo com as 10 camadas, construções, props, vegetação
com LOD por instância, água, céu de Preetham com nuvens, estrelas, lua, luz de ambiente do HDRI do
kit, portal, sacos e projéteis — e os personagens também: os modelos Mixamo (paladina, kachujin, eve),
a leoa, o cão do vazio e o cavalo, com pele na GPU, os clipes já adaptados pela demo, o mixer do three
e as camadas procedurais de `characters.js` (inércia, idle vivo, inclinação no declive, esquivas,
golpes, morte direcional, montado), os tons por facção, as sombras em silhueta e as armas na mão.
Sombras, cobertura do chão e pós-processamento chegaram na fase 7.

**Fase 5 concluída:** a interface é a da demo, em RmlUi desenhado pelo próprio renderizador
(SDL_GPU): `index.html` + `styles.css` viraram `assets/ui/game.rml` + `game.rcss` com os mesmos ids
e classes, e `ui/*.js` virou `GameUi` — carregamento com as fases de `main.js`, título, criação, o
HUD inteiro (minimapa rasterizado do mundo, avisos, barra de ações, mira…), os 16 painéis de
serviço (mercado, bancada, instrutor, armazém, estábulo, quadro, canteiro, astrônoma, viajante,
carga, portal, derrota, as quatro perguntas, inventário, intenções, pausa), o menu (Tab), o Livro e
o mapa completo. Cada botão vira um `Request` que o servidor confere. Com `--dev`, F3 abre as
ferramentas de depuração (Dear ImGui): tempo de quadro, estado do jogador, câmera livre nos
enquadramentos e os comandos de desenvolvimento.

**Fase 6 concluída:** o progresso fica salvo. O servidor grava cada personagem (perfil, posição,
moedas, treinos, bolsa, alforjes, armazém, equipamento, Livro, estatísticas, marcas) e o mundo (hora,
evento, acampamento, céu, mercados) em JSON a cada 20 s, ao sair e ao encerrar — nunca dentro da
Turbulenta, como na demo. O título mostra "Continuar trajetória"; "Nova trajetória" (pausa) apaga o
save e volta ao título. Saves da demo entram com `--import-save` (a migração de `save.js` vale: layout
antigo ou posição inválida voltam ao abrigo, com aviso). Os 23 sons e as duas ambiências de
`audio.js` são sintetizados em C++ e tocam pela SDL3. Sensibilidade e inversão da câmera, qualidade
e mudo ficam em `settings.json`.

**Fase 7 concluída:** a luz e o acabamento da demo.
- **Sombra do sol:** mapa de profundidade que acompanha o jogador, com o castShadow de cada objeto da
  demo; os sombreadores da vegetação até 55 m e os personagens até 36 m.
- **Pós-processamento:** GTAO em meia resolução, bloom do `UnrealBloomPass` e FXAA no lugar do SMAA.
- **Cobertura do chão:** grama, flores, cogumelos, seixos, galhos e grama de campo, que o bake gera
  com a própria `generateCell` da demo.
- **Qualidade:** os três níveis de `quality.js`, com ajuste automático nos primeiros segundos e troca
  na pausa.
- **Pausa:** a cena parada é desenhada uma vez.
- **Medição:** `--benchmark` mede os seis pontos de `measure-benchmark.mjs` nos três níveis; sem GPU,
  o C++ fica à frente da demo nos 18 pontos (tabela em [Desempenho](#desempenho)).

**Fase 8 concluída:** o mesmo jogo pela rede.
- **Servidor e clientes:** `rpg_server --listen` recebe jogadores por UDP (ENet) e cada
  `rpg_local --connect` joga nele, no mesmo mundo.
- **Snapshots:** vão em delta contra o último que o cliente confirmou.
- **Comandos:** saem um por passo do servidor, passam por um validador de taxa e de limites, e o
  próprio personagem é previsto com o mesmo motor do servidor e reconciliado a cada snapshot.
- **Entrada:** o Hello leva a versão do protocolo e o resumo SHA-256 dos dados de design; com dados
  diferentes, o servidor recusa a entrada.
- **Fora do escopo:** contas e banco de dados. O servidor guarda um personagem por nome, em arquivo.

- `rpg_core` (compartilhado por servidor e cliente):
  - dados de design de `config.js`, validados na carga;
  - mundo estático com paridade exata com a demo: relevo escavado pelo rio, pontes, água, biomas,
    24.753 colisores, rotas, zonas e lugares, e a camada de jogo (serviços, coleta, NPCs, grupos,
    portais, Turbulenta, descobertas);
  - motor do personagem, regras de animação, o céu (estrelas e constelações);
  - `JsMath`: seno, cosseno, arco-tangente e exponencial iguais aos do navegador, bit a bit;
  - o protocolo inteiro (comandos, pedidos, eventos, snapshots e sessão).
- `rpg_sim`: toda a simulação da demo, para N jogadores — movimento, ataques, as 36 técnicas,
  projéteis, IA de feras, saqueadores, sombras e guardas, grupos com reaparecimento, inventário,
  mercados, fabricação, reparo, treinos, montaria, derrota por zona, cargas no chão, coleta,
  acampamento reativo, evento regional, estrelas, Região Turbulenta, NPCs, descobertas e o Livro.
- `rpg_server`: sessões, pedidos validados, snapshots com raio de interesse, eventos por
  destinatário, bots e persistência (registros de personagem e mundo, migração e importação do save
  da demo).
- `rpg_client_core` + `rpg_client` (nunca linkam a simulação): sessão pelo protocolo, interpolação
  de snapshots, câmera em terceira pessoa (porte de `updateCamera`), mira, clique para atacar e usar,
  tecla F, a interface da demo em RmlUi (telas, HUD, painéis, menu, Livro, mapas), textos em pt-BR,
  telegrafias e partículas; o pacote visual da demo (cenário, céu, personagens animados) e, sem ele,
  o cenário grey-box da fase 3.
- `rpg_local`: servidor numa thread + cliente, ligados por `LocalTransport`.
- **Testes: 138** (134 sem GPU), entre eles uma trajetória inteira pelo protocolo (comprar, coletar,
  fabricar, cair na Fronteira, recuperar a carga, portal, extração, salvar e retomar). Os de paridade repetem 19 roteiros jogados pela própria demo (do
  boot completo a cada técnica das 18 armas) e exigem o mesmo resultado **bit a bit**, quadro a
  quadro; os da animação refazem os roteiros dos animadores da demo e conferem a pose nó a nó. Os desvios
  intencionais estão em [ARQUITETURA.md](ARQUITETURA.md), seção 8. Os do cliente rodam sem janela; os
  que precisam de GPU (`-DRPG_TEST_GPU=ON`) sobem o jogo inteiro fora da tela.

## Compilar

Pré-requisitos:

- CMake ≥ 3.28 (o 4.x também serve), Ninja e um compilador C++20 (GCC 13 ou mais novo, Clang 17 ou
  MSVC 2022).
- Python 3, para a checagem de camadas.
- Cliente: FreeType e, para compilar shaders, `glslangValidator` (opcional: os SPIR-V vêm prontos em
  `shaders/spv`). A **SDL3** é usada do sistema se houver (Kali, Arch, Fedora 42+); senão o CMake a
  baixa por git (`release-3.4.16`) e compila junto — no Ubuntu 24.04 é esse o caminho. Para rodar, um
  driver Vulkan (qualquer GPU atual; sem GPU, `mesa-vulkan-drivers` dá o lavapipe por software).
  `-DRPG_BUILD_CLIENT=OFF` compila só servidor e testes.
- Node ≥ 18, opcional: sem ele, o teste que confere `cpp/data` com a demo não é registrado.

O C++ não precisa dos arquivos do Git LFS: o pacote de mundo da simulação (`assets/sim`, ~4 MB) e o
pacote visual do cliente (`assets/client`, ~41 MB) já vêm prontos no repositório.

### Linux, com as bibliotecas do sistema

Vale para Ubuntu 24.04+, Debian 13+, Kali rolling e Fedora 39+, que trazem CMake ≥ 3.28 e Catch2 v3 nos
pacotes. O caminho do projeto pode ter espaço e acento (por exemplo `~/Área de trabalho/…`):

```bash
sudo apt install cmake ninja-build g++ python3 nodejs libglm-dev nlohmann-json3-dev catch2 \
  libfreetype-dev libenet-dev glslang-tools libvulkan1 mesa-vulkan-drivers \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev libwayland-dev
# Kali/Debian testing: acrescente libsdl3-dev (a SDL3 do sistema evita o download)
# Fedora: os pacotes equivalentes de glm, nlohmann-json, Catch2 3, freetype, SDL3 e vulkan via dnf
cd cpp
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

No Ubuntu 22.04 (ou outra distro com CMake/Catch2 antigos):

1. Instale um CMake novo com `pip install --user cmake` ou `sudo snap install cmake --classic`.
2. Use o caminho vcpkg abaixo; ele baixa glm, nlohmann-json e Catch2 nas versões certas.

### Qualquer sistema, com vcpkg

```bash
git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh   # (.bat no Windows)
export VCPKG_ROOT=$PWD/vcpkg
cd cpp
cmake --preset dev-vcpkg          # no Windows (Developer PowerShell): --preset windows-msvc
cmake --build --preset dev-vcpkg
ctest --preset dev-vcpkg
```

Outros presets:

- `ci`: RelWithDebInfo, com avisos como erro.
- `asan`: AddressSanitizer + UBSan.
- `release-vcpkg`: Release com as dependências do vcpkg.

## Rodar

### O jogo

```bash
./build/dev/apps/local/rpg_local                 # título → criação → Vale
./build/dev/apps/local/rpg_local --auto          # entra direto com um personagem padrão
./build/dev/apps/local/rpg_local --net-sim latency:120,jitter:30,loss:2   # rede simulada
./build/dev/apps/local/rpg_local --size 1600x900 --fullscreen --no-vsync --dev
./build/dev/apps/local/rpg_local --continue      # retoma o último personagem, sem passar pelo título
./build/dev/apps/local/rpg_local --quality media # alta, media ou baixa (sem o ajuste automático)
```

Saves e preferências ficam em `~/.local/share/projeto-game` (ou `$XDG_DATA_HOME/projeto-game`; no
Windows `%APPDATA%\projeto-game`; no macOS `~/Library/Application Support/projeto-game`):
`world.json`, `characters/<nome>.json` e `settings.json`. `--save-dir DIR` usa outro lugar e
`--no-save` não lê nem grava nada (o padrão das execuções automáticas: `--view`, `--headless`,
`--frames`).

Para trazer um save da demo: no navegador, com a demo aberta, rode no console
`copy(localStorage['projeto-game-demo-v1'])`, cole num arquivo e importe:

```bash
./build/dev/apps/local/rpg_local --import-save save-da-demo.json   # depois: "Continuar trajetória"
```

Controles da demo: WASD anda (Shift corre, Ctrl agacha), Espaço pula (Shift+Espaço: salto
impulsionado), botão direito mira (o clique dispara), clique esquerdo ataca ou usa o que está sob o
cursor e arrastar o chão gira a câmera, Q/E técnicas, C esquiva, 1 poção, F interage, R montaria,
V troca o ombro, Tab+1/2/3 a distância da câmera, Tab (ao soltar) abre o menu, I inventário, B o
Livro, J intenções, M amplia o minimapa, Esc fecha o painel aberto ou pausa. Com `--dev`, F3 abre as
ferramentas de depuração (`--debug-ui` já as abre no começo).

Capturas sem monitor, nos enquadramentos de `demo/captures/after`:

```bash
./build/dev/apps/local/rpg_local --headless --size 1100x700 --view 02_mercado --screenshot mercado.png
# vistas: 01_title_screen 02_mercado 03_ponte_principal … 12b_turbulenta_interior 13_vista_elevada_horizonte
# interface: 01b_create_screen, 14_hud (intenções), 15_inventario, 16_livro, 17_mapa
./build/dev/apps/local/rpg_local --headless --size 1100x700 --auto --dev --hide-hud --portrait --frames 64 \
  --model kachujin --screenshot retrato.png   # o personagem de frente (modelo, pose e arma)
```

### Desempenho

```bash
./build/dev/apps/local/rpg_local --benchmark                 # grava benchmark-cpp.json
python3 tools/compare_benchmark.py ../demo/benchmark-results.json benchmark-cpp.json
```

`--benchmark` roda sem janela (sem vsync). Ele percorre os seis pontos de
`demo/tools/measure-benchmark.mjs` (mercado, ponte principal, garganta, bosque, entreposto norte e
ermos) nos níveis alta, média e baixa. Em cada ponto a câmera livre fica a 8 m do ponto, olhando para o
alvo, e mede 30 quadros (`--bench-frames N`) esperando a GPU terminar cada um, como o `readPixels` da
demo. O resultado traz mediana, p95, triângulos e chamadas de desenho por ponto, no formato de
`demo/benchmark-results.json`.

Medido sem GPU nos dois lados, em 1100×700: a demo no Chrome sem janela com WebGL por software
(`demo/benchmark-results.json`) e o C++ no Mesa lavapipe (4 núcleos, `--bench-frames 10`). Mediana do
tempo de quadro em ms:

| Ponto | Alta: demo | Alta: C++ | Média: demo | Média: C++ | Baixa: demo | Baixa: C++ |
|---|---:|---:|---:|---:|---:|---:|
| mercado | 3705 | 586 | 2437 | 384 | 5887 | 248 |
| ponte principal | 3561 | 1050 | 3130 | 590 | 4855 | 263 |
| garganta | 3913 | 1187 | 2617 | 594 | 2184 | 280 |
| bosque | 3908 | 1270 | 3115 | 775 | 4297 | 516 |
| entreposto norte | 5292 | 972 | 4526 | 546 | 4364 | 239 |
| ermos | 2676 | 306 | 1338 | 247 | 519 | 181 |

O C++ é mais rápido nos 18 pontos (de 2,9× a 24×) e o p95 fica colado na mediana: não há as travadas
de 9–15 s da demo. Numa GPU de verdade, os dois andam muito mais rápido. A comparação vale como
ordem de grandeza: os renderizadores por software não são os mesmos.

### O servidor dedicado

```bash
./build/dev/apps/server/rpg_server               # tempo real, Ctrl+C encerra
./build/dev/apps/server/rpg_server --ticks 300   # 10 s de jogo o mais rápido possível
./build/dev/apps/server/rpg_server --ticks 3600 --bots 8   # 2 min com 8 jogadores controlados pelo servidor
# --data DIR e --sim DIR apontam para outros cpp/data e cpp/assets/sim; --dev aceita comandos de teste
# saves em <diretório do usuário>/servidor (com --ticks, nenhum); --save-dir DIR e --no-save mudam isso
```

### Jogar pela rede

```bash
./build/dev/apps/server/rpg_server --listen               # UDP 27450 (--listen 30000 muda; --max-players N)
./build/dev/apps/local/rpg_local --connect 192.168.0.10   # em cada máquina; :PORTA se não for a 27450
./build/dev/apps/local/rpg_local --net-sim latency:120,jitter:30,loss:2   # jogo local com rede simulada
```

- **Os dois lados** precisam do mesmo `cpp/data`. Com dados diferentes, a entrada é recusada.
- **No `--connect`,** os saves ficam no servidor, um personagem por nome; "Continuar trajetória" retoma
  o personagem com aquele nome. Aqui ficam só as preferências.
- **A pausa** só para a simulação no jogo local de um jogador.
- **ENet:** `libenet-dev` no apt ou `enet` no vcpkg; sem nenhum dos dois, o CMake baixa a v1.3.18.

## Dados de design

`cpp/data` é gerado da demo. Depois de mudar `demo/src/config.js`, rode:

```bash
node demo/tools/export-game-data.mjs          # regrava cpp/data
node demo/tools/export-game-data.mjs --check  # só confere (o ctest roda isto)
```

- **Regras** (`data/*.json`): usadas pelo servidor.
- **Aparência** (`data/presentation`) e **textos** (`data/text/pt-BR`): só o cliente lê.

## Pacote de mundo da simulação

`cpp/assets/sim` também vem da demo. `demo/tools/bake-sim-world.mjs` roda no Node a própria sequência
de boot da demo e grava o resultado:

- relevo já escavado pelo rio e máscara de biomas;
- rio, pontes e campo de rotas;
- colisores de construções, árvores, acréscimos e objetos de jogo, na ordem de inserção;
- lugares e pontos de zona.

Assim o servidor não precisa de nenhum código de vegetação ou de render. O mesmo script grava a
camada de jogo (`assets/sim/gameplay-layout.json`) e as amostras dos testes de paridade:
`tests/parity/fixtures/world-parity.json` (consultas do mundo) e `sim-parity.json` (19 roteiros
jogados pelas funções da demo, com o rastro quadro a quadro).

Rode de novo quando o mundo da demo mudar. Isso precisa do Git LFS, porque o kit de natureza e os
modelos estão lá:

```bash
git lfs pull --include="demo/assets/**"
(cd demo/tools && npm ci)                  # inclui o esbuild 0.28.0 do build da demo
node demo/tools/bake-sim-world.mjs         # regrava assets/sim, as fixtures e text/pt-BR/places.json
node demo/tools/bake-sim-world.mjs --check # só confere
```

Referencial: cada mapa usa as coordenadas da demo (a Turbulenta continua centrada em x = 1.400). Assim
a paridade fica bit a bit. Como cada entidade carrega o próprio mapa, nenhuma regra depende de
`x > 1000`.

## Pacote visual do cliente

`cpp/assets/client` também vem da demo, mas do navegador: `demo/tools/bake-client-scene.mjs` abre a
montagem de cena da própria demo no Chromium do Playwright (texturas procedurais, GLB, o mundo
inteiro, a Turbulenta, os modelos de jogo) e exporta a cena pronta:

- `scene.json`: geometrias, materiais (com os recursos de `patchMaterial`: tri, occ, splat, wind, water,
  shore), texturas, blocos de relevo, malhas, instâncias, campos de LOD, modelos dinâmicos e o HDRI do kit;
- `scene.bin`: fluxos comprimidos com o codec do meshoptimizer (sem perdas). O relevo leva só os pesos
  das camadas; a malha é refeita do heightfield e confere por hash com a da demo;
- `tex/*.webp`: texturas em WebP q95 com alfa sem perdas (`--lossless` grava tudo sem perdas, ~54 MB);
- os personagens como a demo os deixa prontos (`models.js` + `makeHumanoid`): nós e pose de repouso,
  malhas com pele (bindMatrix, ossos, inversas), os clipes já adaptados a cada modelo (retarget e
  contato), a pose ociosa de referência, os ciclos laterais e as empunhaduras das armas. Por isso o C++
  não lê os GLB (o plano previa fastgltf): o que a demo calcula na carga já vem calculado.

O mesmo script grava `tests/parity/fixtures/anim-parity.json`: roteiros de entrada rodados nos
animadores da própria demo (`animateHumanoid`, `animateBeast`, `animateMount`), com a pose de cada nó e
vértices com pele no mundo. `AnimTests` refaz os roteiros em C++ e confere nó a nó.

```sh
git lfs pull --include="demo/assets/**"
(cd demo/tools && npm ci)                       # sharp e meshoptimizer; Playwright 1.5x instalado
node demo/tools/bake-client-scene.mjs           # regrava cpp/assets/client (~30 s)
node demo/tools/bake-client-scene.mjs --check   # só confere
```

Capturas para comparar com a demo: `node demo/tools/capture-reference.mjs DIR` grava os 14
enquadramentos da demo atual (Chromium com SwiftShader, alguns minutos por enquadramento; `--only
portrait` grava o retrato do personagem), e `python3 cpp/tools/compare_captures.py DIR shots --out
relatorio` compara com as do `rpg_local`.

## Regras de camada

`python3 tools/check_layering.py` roda no `ctest` e falha se:

- `core` ou `sim` incluírem rede, servidor ou cliente;
- o cliente incluir a simulação;
- qualquer camada fora do cliente incluir biblioteca de janela, GPU ou UI;
- dentro do cliente, algo fora de `client/platform`, `client/render`, `client/audio` e `client/app`
  incluir a SDL (o `rpg_client_core` é testável sem janela).
