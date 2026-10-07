#pragma once
// Modelos do pacote para o que o servidor replica (sem SDL; testável):
//   - o rasgo violeta (turbulent.js `portalMesh`), no chão onde o portal abriu;
//   - os sacos de carga (zones.js `createLootBag`), o próprio com a cor de quem morreu;
//   - os projéteis (combat.js `shoot`): flecha por dono, virote e magia pela escola, apontados na
//     direção do voo (setFromUnitVectors de +Z).
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "client/assets/ScenePack.h"
#include "client/world/PackScene.h"
#include "core/protocol/Snapshot.h"

namespace rpg {
class StaticWorld;
}

namespace rpg::client {

class ClientWorld;

// Chave do modelo de um projétil em `ScenePack::projectiles` a partir de EntityState::type.
std::string projectileModelKey(std::uint16_t type);

// Rotação que leva +Z até `dir` (Quaternion.setFromUnitVectors do three).
glm::mat4 rotationFromZ(glm::vec3 dir);

void collectEntityModels(const ScenePack& pack, const ClientWorld& world, const StaticWorld& statics, std::vector<ModelInstance>& out);

}  // namespace rpg::client
