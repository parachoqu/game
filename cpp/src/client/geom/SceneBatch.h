#pragma once
// O que o renderizador desenha num quadro, já em listas prontas para a GPU:
//   - instâncias de primitivas iluminadas (cenário grey-box, personagens, projéteis, sacos);
//   - geometria sem luz (telegrafias, anel de seleção, partículas).
// Montado na CPU a partir do layout estático e do mundo replicado; testável sem janela.
#include <array>
#include <cstdint>
#include <vector>

#include "client/geom/MeshData.h"

namespace rpg {
class StaticMap;
class StaticWorld;
struct GameplayLayout;
struct GameData;
}  // namespace rpg

namespace rpg::client {

class ClientWorld;
class FxState;
struct Presentation;

// O que o montador de cena consulta (tudo somente leitura).
struct SceneContext {
  const StaticWorld* statics = nullptr;
  const GameData* data = nullptr;
  const Presentation* look = nullptr;
  // Com o pacote visual, pontos de coleta, santuários, saídas, portal, sacos e projéteis vêm dele.
  bool packModels = false;
  // Com os moldes do pacote, jogadores, inimigos, NPCs e montarias vêm de CharacterViews.
  bool packCharacters = false;
};

enum class Prim : std::uint8_t { Box, Cylinder, Cone, Sphere, Capsule, Ring, Count };
inline constexpr std::size_t kPrimCount = static_cast<std::size_t>(Prim::Count);

struct InstanceLists {
  std::array<std::vector<Instance>, kPrimCount> byPrim;
  void clear() {
    for (auto& v : byPrim) v.clear();
  }
  void add(Prim p, const Instance& i) { byPrim[static_cast<std::size_t>(p)].push_back(i); }
  std::size_t total() const {
    std::size_t n = 0;
    for (const auto& v : byPrim) n += v.size();
    return n;
  }
};

// Cenário estático do mapa: colisores (árvores, rochas, construções, muros) e objetos de jogo do
// layout (serviços, pontos de coleta, santuários e saídas da Turbulenta).
InstanceLists buildStaticProps(const StaticMap& map, const GameplayLayout& layout);

// Entidades replicadas deste quadro (personagens, montarias, NPCs, projéteis, sacos, portal).
void buildEntityInstances(const SceneContext& ctx, const ClientWorld& world, double time, InstanceLists& out);

// Efeitos sem luz: telegrafias no chão, anel de seleção, partículas.
struct HoverRing {
  bool visible = false;
  double x = 0, z = 0, r = 1;
  std::uint32_t color = 0xff5a3c;
};
void buildFxGeometry(const FxState& fx, const StaticMap& map, std::uint8_t mapKind, const HoverRing& ring, double time,
                     std::vector<ColorVertex>& out);

}  // namespace rpg::client
