#include "core/world/Terrain.h"

namespace rpg {

MapTerrain::MapTerrain(MapKind kind, Heightfield height, double originX, double originY, double originZ, double planZSign)
    : kind_(kind), height_(std::move(height)), originX_(originX), originY_(originY), originZ_(originZ), planZSign_(planZSign) {}

void MapTerrain::setRegionExtras(BiomeMask mask, River river, std::vector<Bridge> bridges) {
  hasRegionExtras_ = true;
  mask_ = std::move(mask);
  river_ = std::move(river);
  bridges_ = std::move(bridges);
}

// heightfield.js regionHeightAt / turbulentHeightAt:
//   região:     sampleField(REGION, x, PLAN_Z_SIGN · z)
//   Turbulenta: sampleField(TURB, x − off.x, PLAN_Z_SIGN · (z − off.z)) + off.y
double MapTerrain::terrainHeight(double x, double z) const {
  if (kind_ == MapKind::Region) return height_.sample(x, planZSign_ * z);
  return height_.sample(x - originX_, planZSign_ * (z - originZ_)) + originY_;
}

// terrain.js groundHeight, linha a linha.
double MapTerrain::groundHeight(double x, double z) const {
  double h = terrainHeight(x, z);
  for (const Bridge& b : bridges_) {
    const double dx = x - b.x, dz = z - b.z;
    double lx = dx, lz = dz;
    if (b.rot != 0.0) {
      lx = dx * b.c - dz * b.s;
      lz = dx * b.s + dz * b.c;
    }
    const double ax = lx < 0 ? -lx : lx, az = lz < 0 ? -lz : lz;
    if (ax > b.hw + b.apronSide || az > b.hd + b.apron) continue;
    const double wx = ax <= b.hw ? 1 : 1 - (ax - b.hw) / b.apronSide;
    const double wz = az <= b.hd ? 1 : 1 - (az - b.hd) / b.apron;
    const double w = wx < wz ? wx : wz;
    if (w <= 0) continue;
    const double t = w * w * (3 - 2 * w);
    const double y = h + (b.y - h) * t;
    if (y > h) h = y;
  }
  return h;
}

double MapTerrain::waterAt(double x, double z) const {
  if (kind_ != MapKind::Region) return kNoWater;
  return isWaterCell(x, z) ? river_.levelAt(x) : kNoWater;
}

int MapTerrain::biomeAt(double x, double z) const {
  if (!hasRegionExtras_) return 0;
  return mask_.nibble(x, planZSign_ * z) & 7;
}

bool MapTerrain::isWaterCell(double x, double z) const {
  if (!hasRegionExtras_) return false;
  return (mask_.nibble(x, planZSign_ * z) & 8) != 0;
}

// heightfield.js slopeAt (sobre o relevo da região, sem pontes)
double MapTerrain::slopeAt(double x, double z, double d) const {
  const double h = terrainHeight(x, z);
  const double a = terrainHeight(x + d, z) - h, b = terrainHeight(x, z + d) - h;
  return (a < 0 ? -a : a) + (b < 0 ? -b : b);
}

}  // namespace rpg
