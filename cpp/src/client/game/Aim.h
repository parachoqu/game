#pragma once
// Mira (game/player.js `updateAim`): raio da câmera pelo cursor contra inimigos (esfera em volta do
// peito) e contra o relevo (passos de 2,2 m + bissecção). O resultado vai no PlayerCommand; o
// servidor usa a direção e confere o alcance.
#include <cstdint>

#include <glm/glm.hpp>

#include "client/ClientWorld.h"
#include "client/game/ViewCamera.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

struct AimResult {
  double yaw = 0, pitch = 0, dist = 0;
  glm::dvec3 point{0.0};
  std::uint32_t enemy = 0;
};

AimResult computeAim(const Ray& ray, const glm::dvec3& playerPos, double camYaw, const ClientWorld& w,
                     const GameData& data, const StaticMap& map);

}  // namespace rpg::client
