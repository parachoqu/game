#pragma once
// Alvos do cliente: o que dá para usar por perto (tecla F, clique, anel) e o que dá para atacar.
// Espelha game/interact.js `targetsNear` e pointer.js `pick` a partir do layout estático e do estado
// replicado; o servidor confere cada pedido com a lista dele.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "client/ClientWorld.h"
#include "client/game/ViewCamera.h"
#include "core/data/GameData.h"
#include "core/protocol/Interact.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

struct UseTarget {
  protocol::InteractKind kind = protocol::InteractKind::Market;
  std::uint32_t id = 0;
  double x = 0, z = 0, r = 0, bias = 0;
  bool usable = true;                // nó esgotado aparece, mas não se usa
  std::int32_t layoutIndex = -1;     // interagível estático: índice no layout (rótulo)
  std::int32_t travelerIndex = -1;   // viajante: índice em layout.travelers
};

std::vector<UseTarget> targetsNear(const ClientWorld& w, const StaticWorld& statics, double maxDist);
// Mais próximo dentro do raio de uso (interact.js `nearestInRange`): o prompt da tecla F.
std::optional<UseTarget> nearestInRange(const ClientWorld& w, const StaticWorld& statics);

// Inimigo que pode ser mirado/clicado (vivo, acordado e que não é guarda).
bool attackable(const protocol::EntityState& e, const GameData& data);
double enemyBarY(const protocol::EntityState& e);    // altura da barra (enemies.js `barY`)
double enemyRadius(const protocol::EntityState& e);  // raio do corpo

// O que está sob o cursor (pointer.js `pick`): inimigo tem prioridade sobre objeto.
struct Hover {
  enum class Kind : std::uint8_t { Enemy, Use } kind = Kind::Enemy;
  std::uint32_t enemy = 0;
  UseTarget use;
};
std::optional<Hover> pick(const ClientWorld& w, const StaticWorld& statics, const GameData& data, const ViewCamera& cam,
                          double mouseX, double mouseY, double width, double height);

}  // namespace rpg::client
