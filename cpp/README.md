# Projeto Game — C++20

Reescrita da demo (`../demo`, JavaScript + three.js) em C++20. A simulação fica separada do render e
do input desde o início, para o jogo virar RPG online sem reescrita. A arquitetura, as camadas e o
plano de fases estão em [ARQUITETURA.md](ARQUITETURA.md).

## Estado

**Fase 0 e Fase 1a concluídas:**

- CMake com os alvos `rpg_core`, `rpg_net`, `rpg_sim` e `rpg_server`, e o executável `rpg_server`
  (servidor headless).
- `rpg_core` (compartilhado por servidor e cliente):
  - `Math`: funções numéricas de `state.js`, mais o `Math.hypot` e o `Math.round` do V8 reproduzidos
    bit a bit.
  - `Rng`: `Mulberry32` bit a bit igual ao JS, e `Pcg32` para a simulação.
  - `Noise`: o ruído de `world/noise.js`.
  - `GameClock`, `FixedTimestep` e `SlotMap`, este com ordem estável como os arrays do JS.
  - `GameData`: todos os dados de design de `config.js`, validados na carga.
  - **Mundo estático** (`core/world`), com um relevo, colisores e regras próprios para cada mapa
    (região e Turbulenta):
    - relevo escavado pelo rio, com pontes e rampas;
    - água e biomas;
    - 24.753 colisores na ordem da demo;
    - campo de rotas, zonas de risco e nomes de lugar.
  - `moveEntity` (`core/movement`), o mesmo passo usado pelo servidor e, no futuro, pela predição do
    cliente.
- `rpg_net`: `LocalTransport` com latência, jitter e perda simulados.
- `rpg_sim` / `rpg_server`: `World` em passo fixo sobre o mundo estático, e `ServerHost` (thread
  própria, sessões).
- **Testes: 46 casos.** A paridade com o JS é exata (`==`, sem tolerância):
  - relevo, pontes, água, rotas, biomas, zonas e lugares em ~2.700 pontos;
  - `testPoint`/`resolve` em 2.500 casos;
  - `moveEntity` em 91 trajetos de 45 passos.

  Também rodam a checagem de camadas e a conferência dos dados com a demo.

O cliente (janela SDL3 + SDL_GPU) entra na fase 3.

## Compilar

Pré-requisitos:

- CMake ≥ 3.28, Ninja e um compilador C++20 (GCC 13, Clang 17 ou MSVC 2022).
- Python 3, para a checagem de camadas.
- Node ≥ 18, opcional: sem ele, o teste que confere `cpp/data` com a demo não é registrado.

O C++ não precisa dos arquivos do Git LFS: o pacote de mundo da simulação (`assets/sim`, ~4 MB) já vem
pronto no repositório.

### Linux, com as bibliotecas do sistema

Vale para Ubuntu 24.04+, Debian 13+ e Fedora 39+, que trazem CMake ≥ 3.28 e Catch2 v3 nos pacotes:

```bash
sudo apt install cmake ninja-build g++ python3 nodejs libglm-dev nlohmann-json3-dev catch2
# Fedora: os pacotes equivalentes de glm, nlohmann-json e Catch2 3 via dnf
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

```bash
./build/dev/apps/server/rpg_server               # tempo real, Ctrl+C encerra
./build/dev/apps/server/rpg_server --ticks 300   # 10 s de jogo o mais rápido possível
# --data DIR e --sim DIR apontam para outros cpp/data e cpp/assets/sim
```

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

Assim o servidor não precisa de nenhum código de vegetação ou de render. O mesmo script gera as
amostras dos testes de paridade (`tests/parity/fixtures/world-parity.json`).

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

## Regras de camada

`python3 tools/check_layering.py` roda no `ctest` e falha se:

- `core` ou `sim` incluírem rede, servidor ou cliente;
- o cliente incluir a simulação;
- qualquer camada fora do cliente incluir biblioteca de janela, GPU ou UI.
