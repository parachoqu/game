#include "client/game/Aim.h"

#include <cmath>

#include "client/game/Targets.h"
#include "core/Math.h"

namespace rpg::client {

AimResult computeAim(const Ray& ray, const glm::dvec3& P, double camYaw, const ClientWorld& w, const GameData& data,
                     const StaticMap& map) {
  const glm::dvec3 o = ray.origin, d = ray.dir;

  // 1. inimigos
  std::uint32_t bestEnemy = 0;
  double bestEnemyDist = 70;
  glm::dvec3 bestEnemyPt(0.0);
  for (const auto& e : w.entities()) {
    if (!attackable(e, data)) continue;
    const glm::dvec3 c(e.x, e.y + enemyBarY(e) * 0.45, e.z);
    const double t = glm::dot(d, c - o);
    if (t > 0.8 && t < bestEnemyDist) {
      const glm::dvec3 pt = o + d * t;
      if (glm::length(pt - c) < enemyRadius(e) + 0.65) {
        bestEnemy = e.id;
        bestEnemyDist = t;
        bestEnemyPt = c;
      }
    }
  }

  // 2. relevo
  double terrainHitDist = 70;
  bool hasTerrainHit = false;
  double prevD = 1.0;
  for (double s = 2.0; s <= 70; s += 2.2) {
    const glm::dvec3 p = o + d * s;
    if (p.y <= map.groundHeight(p.x, p.z)) {
      double l = prevD, r = s;
      for (int b = 0; b < 5; ++b) {
        const double m = (l + r) * 0.5;
        const glm::dvec3 q = o + d * m;
        if (q.y <= map.groundHeight(q.x, q.z)) r = m;
        else l = m;
      }
      terrainHitDist = (l + r) * 0.5;
      hasTerrainHit = true;
      break;
    }
    prevD = s;
  }

  AimResult a;
  if (bestEnemy && bestEnemyDist <= terrainHitDist + 0.5) {
    a.point = bestEnemyPt;
    a.enemy = bestEnemy;
  } else if (hasTerrainHit) {
    a.point = o + d * terrainHitDist;
  } else {
    a.point = o + d * 65.0;
  }
  const double dx = a.point.x - P.x, dz = a.point.z - P.z;
  const double horiz = std::hypot(dx, dz);
  // muito perto do personagem, a paralaxe da câmera lateral é atenuada
  const double camFwdYaw = std::atan2(-std::sin(camYaw), -std::cos(camYaw));
  if (horiz < 2.4) {
    const double blend = std::max(0.0, (horiz - 0.6) / 1.8);
    a.yaw = lerpAngle(camFwdYaw, std::atan2(dx, dz), blend);
  } else {
    a.yaw = std::atan2(dx, dz);
  }
  const double dy = a.point.y - (P.y + 1.45);
  a.pitch = clamp(std::atan2(dy, std::max(1.2, horiz)), -0.85, 0.85);
  a.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  return a;
}

}  // namespace rpg::client
