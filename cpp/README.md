# Projeto Game — C++20

Reescrita da demo (`../demo`, JavaScript + three.js) em C++20. A simulação fica separada do render e
do input desde o início, para o jogo virar RPG online sem reescrita. A arquitetura, as camadas e o
plano de fases estão em [ARQUITETURA.md](ARQUITETURA.md).

## Estado

**Fases 0, 1 e 2 concluídas:** o jogo inteiro da demo roda no servidor, sem janela.

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
  destinatário e bots.
- **Testes: 81.** Os de paridade repetem 19 roteiros jogados pela própria demo (do boot completo a
  cada técnica das 18 armas) e exigem o mesmo resultado **bit a bit**, quadro a quadro. Os desvios
  intencionais estão em [ARQUITETURA.md](ARQUITETURA.md), seção 8.

O cliente (janela SDL3 + SDL_GPU) entra na fase 3.

## Compilar

Pré-requisitos:

- CMake ≥ 3.28 (o 4.x também serve), Ninja e um compilador C++20 (GCC 13 ou mais novo, Clang 17 ou
  MSVC 2022).
- Python 3, para a checagem de camadas.
- Node ≥ 18, opcional: sem ele, o teste que confere `cpp/data` com a demo não é registrado.

O C++ não precisa dos arquivos do Git LFS: o pacote de mundo da simulação (`assets/sim`, ~4 MB) já vem
pronto no repositório.

### Linux, com as bibliotecas do sistema

Vale para Ubuntu 24.04+, Debian 13+, Kali rolling e Fedora 39+, que trazem CMake ≥ 3.28 e Catch2 v3 nos
pacotes. O caminho do projeto pode ter espaço e acento (por exemplo `~/Área de trabalho/…`):

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
./build/dev/apps/server/rpg_server --ticks 3600 --bots 8   # 2 min com 8 jogadores controlados pelo servidor
# --data DIR e --sim DIR apontam para outros cpp/data e cpp/assets/sim; --dev aceita comandos de teste
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

## Regras de camada

`python3 tools/check_layering.py` roda no `ctest` e falha se:

- `core` ou `sim` incluírem rede, servidor ou cliente;
- o cliente incluir a simulação;
- qualquer camada fora do cliente incluir biblioteca de janela, GPU ou UI.
