#pragma once
// Pacote de mundo da simulação (cpp/assets/sim), gerado pela demo com
// `node demo/tools/bake-sim-world.mjs`: relevo já escavado pelo rio, máscara de biomas, rio,
// campo de rotas, pontes, colisores na ordem de inserção, lugares e pontos de zona.
//
// Este é o formato cru, como está no disco; `StaticWorld` monta as estruturas de consulta.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/world/CollisionGrid.h"
#include "core/world/Heightfield.h"
#include "core/world/RouteField.h"
#include "core/world/Terrain.h"
#include "core/world/ZoneMap.h"

namespace rpg {

struct SimPack {
  int schema = 0;
  int worldLayoutVersion = 0;
  std::string runtimeSchema;

  // referencial da demo (world/coordinates.js)
  double planZSign = -1;
  double turbulentOffsetX = 0, turbulentOffsetY = 0, turbulentOffsetZ = 0;
  double turbulentGateX = 0;

  struct Region {
    double half = 0;
    Heightfield::Meta heightMeta;
    std::vector<std::uint16_t> height;
    int maskW = 0;
    std::vector<std::uint8_t> mask;
    double riverX0 = 0;
    std::vector<float> riverLevel, riverCenter, riverHalf;
    int routeN = 0;
    double routeStep = 0, routeOrigin = 0;
    std::vector<std::uint8_t> routeField;
    std::vector<Polyline> routes;
  } region;

  struct Turbulent {
    double half = 0;
    Heightfield::Meta heightMeta;
    std::vector<std::uint16_t> height;
    std::vector<Polyline> routes;
  } turbulent;

  std::vector<Bridge> bridges;
  std::vector<Collider> colliders;  // ordem de inserção da demo
  std::map<std::string, PlaceArea> places;
  std::vector<Circle> protectedPoints, neverProtected;

  // Lê simpack.json e os binários. DataError com o arquivo em caso de problema.
  static SimPack load(const std::filesystem::path& dir);
};

}  // namespace rpg
