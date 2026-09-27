# Projeto Game — C++20

Reescrita da demo (`../demo`, JavaScript + three.js) em C++20. A simulação fica separada do render e
do input desde o início, para o jogo virar RPG online sem reescrita. A arquitetura, as camadas e o
plano de fases estão em [ARQUITETURA.md](ARQUITETURA.md).

## Estado

**Fase 0 concluída e início da fase 1:**

- CMake com os alvos `rpg_core`, `rpg_net`, `rpg_sim` e `rpg_server`, e o executável `rpg_server`
  (servidor headless).
- `rpg_core` (compartilhado por servidor e cliente):
  - `Math`: funções numéricas de `state.js`.
  - `Rng`: `Mulberry32` bit a bit igual ao JS, e `Pcg32` para a simulação.
  - `GameClock`: o `clock()` de `state.js`.
  - `FixedTimestep` e `SlotMap`, este com ordem estável como os arrays do JS.
  - `GameData`: todos os dados de design de `config.js`, validados na carga.
- `rpg_net`: `LocalTransport` com latência, jitter e perda simulados.
- `rpg_sim` / `rpg_server`: `World` em passo fixo e `ServerHost` (thread própria, sessões).
- Dados: `data/*.json`, gerados de `demo/src/config.js` por `demo/tools/export-game-data.mjs`.
- Testes: 32 casos, incluindo paridade com o JS, a checagem de camadas e a de dados em dia com a demo.

O cliente (janela SDL3 + SDL_GPU) entra na fase 3.

## Compilar

Pré-requisitos: CMake ≥ 3.28, Ninja e um compilador C++20 (GCC 13, Clang 17 ou MSVC 2022).

### Linux, com as bibliotecas do sistema

```bash
sudo apt install libglm-dev nlohmann-json3-dev catch2 ninja-build   # Ubuntu 24.04
cd cpp
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

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
```

## Dados de design

`cpp/data` é gerado da demo. Depois de mudar `demo/src/config.js`, rode:

```bash
node demo/tools/export-game-data.mjs          # regrava cpp/data
node demo/tools/export-game-data.mjs --check  # só confere (o ctest roda isto)
```

- **Regras** (`data/*.json`): usadas pelo servidor.
- **Aparência** (`data/presentation`) e **textos** (`data/text/pt-BR`): só o cliente lê.

## Regras de camada

`python3 tools/check_layering.py` roda no `ctest` e falha se:

- `core` ou `sim` incluírem rede, servidor ou cliente;
- o cliente incluir a simulação;
- qualquer camada fora do cliente incluir biblioteca de janela, GPU ou UI.
