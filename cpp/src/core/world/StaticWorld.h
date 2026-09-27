#pragma once
// O mundo estático, somente leitura, compartilhado por servidor (autoridade) e cliente (predição,
// câmera, mira): relevo, água, pontes, colisores, rotas, zonas e lugares de cada mapa.
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include "core/Types.h"
#include "core/data/Defs.h"
#include "core/world/CollisionGrid.h"
#include "core/world/GameplayLayout.h"
#include "core/world/MapId.h"
#include "core/world/RouteField.h"
#include "core/world/Terrain.h"
#include "core/world/ZoneMap.h"

namespace rpg {

class StaticMap {
 public:
  StaticMap(MapKind kind, MapTerrain terrain, CollisionGrid collision, std::optional<RouteField> routeField,
            std::vector<Polyline> routes, XZ center, double moveLimit, Balance::Collision rules);

  MapKind kind() const { return kind_; }
  const MapTerrain& terrain() const { return terrain_; }
  const CollisionGrid& collision() const { return collision_; }
  const std::vector<Polyline>& routes() const { return routes_; }
  const Balance::Collision& rules() const { return rules_; }

  double groundHeight(double x, double z) const { return terrain_.groundHeight(x, z); }
  double waterAt(double x, double z) const { return terrain_.waterAt(x, z); }

  // Distância até a estrada mais próxima; só a região tem estradas com rampa de inclinação maior.
  bool hasRouteField() const { return routeField_.has_value(); }
  double routeDistance(double x, double z) const;

  // Limite de movimento (collide.js): a entidade fica a `moveLimit` do centro em x e em z.
  XZ clampToBounds(XZ p) const;
  XZ center() const { return center_; }
  double moveLimit() const { return moveLimit_; }

 private:
  MapKind kind_;
  MapTerrain terrain_;
  CollisionGrid collision_;
  std::optional<RouteField> routeField_;
  std::vector<Polyline> routes_;
  XZ center_;
  double moveLimit_;
  Balance::Collision rules_;
};

class StaticWorld {
 public:
  // Lê cpp/assets/sim (ver SimPack). `rules` vem de GameData::balance.collision.
  static StaticWorld load(const std::filesystem::path& simDir, const Balance::Collision& rules);

  const StaticMap& map(MapKind kind) const { return maps_[static_cast<std::size_t>(kind)]; }

  ZoneKind zoneAt(MapKind map, double x, double z) const { return zones_.zoneAt(map, x, z); }
  PlaceId placeAt(MapKind map, double x, double z) const {
    return zones_.placeAt(map, x, z, maps_[0].terrain().river());
  }
  const ZoneMap& zones() const { return zones_; }

  // Referencial da demo: pontos com x além da fronteira técnica são da Turbulenta. Só serve para
  // ler dados e saves da demo e para os testes de paridade; as regras usam o mapa da entidade.
  MapKind mapAtDemoX(double x) const { return x > gateX_ ? MapKind::Turbulent : MapKind::Region; }

  int worldLayoutVersion() const { return worldLayoutVersion_; }

  // Camada de jogo (serviços, coleta, NPCs, grupos, portais…): gameplay-layout.json.
  const GameplayLayout& layout() const { return layout_; }

 private:
  StaticWorld(std::array<StaticMap, kMapCount> maps, ZoneMap zones, double gateX, int layoutVersion, GameplayLayout layout)
      : maps_(std::move(maps)), zones_(std::move(zones)), gateX_(gateX), worldLayoutVersion_(layoutVersion),
        layout_(std::move(layout)) {}

  std::array<StaticMap, kMapCount> maps_;
  ZoneMap zones_;
  double gateX_;
  int worldLayoutVersion_;
  GameplayLayout layout_;
};

}  // namespace rpg
