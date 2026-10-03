#include "core/world/StaticWorld.h"

#include <algorithm>
#include <limits>

#include "core/data/DataError.h"
#include "core/world/SimPack.h"

namespace rpg {

StaticMap::StaticMap(MapKind kind, MapTerrain terrain, CollisionGrid collision, std::optional<RouteField> routeField,
                     std::vector<Polyline> routes, XZ center, double half, double moveLimit, Balance::Collision rules)
    : kind_(kind),
      terrain_(std::move(terrain)),
      collision_(std::move(collision)),
      routeField_(std::move(routeField)),
      routes_(std::move(routes)),
      center_(center),
      half_(half),
      moveLimit_(moveLimit),
      rules_(rules) {}

double StaticMap::routeDistance(double x, double z) const {
  return routeField_ ? routeField_->distance(x, z) : std::numeric_limits<double>::infinity();
}

// collide.js: p.x = Math.max(c − lim, Math.min(c + lim, p.x)) (e o mesmo em z)
XZ StaticMap::clampToBounds(XZ p) const {
  p.x = std::max(center_.x - moveLimit_, std::min(center_.x + moveLimit_, p.x));
  p.z = std::max(center_.z - moveLimit_, std::min(center_.z + moveLimit_, p.z));
  return p;
}

namespace {

const PlaceArea& place(const SimPack& p, const char* key) {
  const auto it = p.places.find(key);
  if (it == p.places.end()) throw DataError(std::string("simpack.json › places: falta '") + key + "'");
  return it->second;
}

const Polyline& route(const std::vector<Polyline>& routes, const char* id) {
  for (const Polyline& r : routes)
    if (r.id == id) return r;
  throw DataError(std::string("simpack.json › region.routes: falta '") + id + "'");
}

}  // namespace

StaticWorld StaticWorld::load(const std::filesystem::path& simDir, const Balance::Collision& rules) {
  SimPack p = SimPack::load(simDir);

  // Relevo, máscara, rio e pontes da região; relevo da Turbulenta no referencial deslocado.
  MapTerrain region(MapKind::Region, Heightfield(std::move(p.region.height), p.region.heightMeta), 0, 0, 0, p.planZSign);
  region.setRegionExtras(
      BiomeMask(std::move(p.region.mask), p.region.maskW, p.region.heightMeta.x0, p.region.heightMeta.y0),
      River(p.region.riverX0, std::move(p.region.riverLevel), std::move(p.region.riverCenter), std::move(p.region.riverHalf)),
      std::move(p.bridges));
  MapTerrain turbulent(MapKind::Turbulent, Heightfield(std::move(p.turbulent.height), p.turbulent.heightMeta),
                       p.turbulentOffsetX, p.turbulentOffsetY, p.turbulentOffsetZ, p.planZSign);

  // Colisores na ordem da demo, separados por mapa (a Turbulenta fica além da fronteira técnica).
  CollisionGrid regionGrid(rules.cell), turbulentGrid(rules.cell);
  for (const Collider& c : p.colliders) (c.x > p.turbulentGateX ? turbulentGrid : regionGrid).add(c);

  ZoneMap::Data z;
  z.vale = place(p, "vale");
  z.mercado = place(p, "mercado");
  z.alto = place(p, "alto");
  z.canteiro = place(p, "canteiro");
  z.passagem = place(p, "passagem");
  z.acampamento = place(p, "acampamento");
  z.observatorio = place(p, "observatorio");
  z.mina = place(p, "mina");
  z.caverna = place(p, "caverna");
  z.portal = place(p, "portal");
  z.ponteMenor = place(p, "ponteMenor");
  z.ermos = place(p, "ermos");
  z.bosque = place(p, "bosque");
  if (z.ermos.rx <= 0 || z.ermos.rz <= 0) throw DataError("simpack.json › places › ermos: sem semieixos rx/rz");
  z.protectedPoints = p.protectedPoints;
  z.neverProtected = p.neverProtected;
  z.eastRoad = route(p.region.routes, "safe_east").pts;   // layout.js EAST_ROAD
  z.mainRoad = route(p.region.routes, "short_gorge").pts; // layout.js MAIN_ROAD

  // Limites de movimento de collide.js: HALF − 8 na região; TURB.half − 6 em volta do centro da Turbulenta.
  StaticMap regionMap(MapKind::Region, std::move(region), std::move(regionGrid),
                      RouteField(std::move(p.region.routeField), p.region.routeN, p.region.routeStep, p.region.routeOrigin),
                      std::move(p.region.routes), XZ{0, 0}, p.region.half, p.region.half - 8, rules);
  StaticMap turbulentMap(MapKind::Turbulent, std::move(turbulent), std::move(turbulentGrid), std::nullopt,
                         std::move(p.turbulent.routes), XZ{p.turbulentOffsetX, p.turbulentOffsetZ}, p.turbulent.half, p.turbulent.half - 6, rules);

  GameplayLayout layout = GameplayLayout::load(simDir / "gameplay-layout.json");
  return StaticWorld({std::move(regionMap), std::move(turbulentMap)}, ZoneMap(std::move(z)), p.turbulentGateX,
                     p.worldLayoutVersion, std::move(layout));
}

}  // namespace rpg
