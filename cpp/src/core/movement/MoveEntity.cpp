#include "core/movement/MoveEntity.h"

#include "core/Math.h"

namespace rpg {

void moveEntity(const StaticMap& map, XZ& p, double dx, double dz, double radius, double lift) {
  const Balance::Collision& rules = map.rules();
  const double h0 = map.groundHeight(p.x, p.z);
  const auto tryMove = [&](double mx, double mz) {
    const double nx = p.x + mx, nz = p.z + mz;
    const double step = jsHypot(mx, mz);
    if (step < 1e-5) return false;
    const double h1 = map.groundHeight(nx, nz);
    // sair da água para a margem é sempre permitido; fora dela vale o limite de inclinação
    const bool inWater = map.waterAt(p.x, p.z) > h0 + 0.2;
    const double limit =
        map.hasRouteField() && map.routeDistance(nx, nz) < rules.routeLane ? rules.routeSlope : rules.maxSlope;
    if (h1 - h0 - lift > limit * (step > 0.05 ? step : 0.05) && !inWater) return false;
    p.x = nx;
    p.z = nz;
    return true;
  };
  if (!tryMove(dx, dz)) {
    if (!tryMove(dx, 0)) tryMove(0, dz);
  }
  p = map.collision().resolve(p.x, p.z, radius);
  p = map.clampToBounds(p);
}

}  // namespace rpg
