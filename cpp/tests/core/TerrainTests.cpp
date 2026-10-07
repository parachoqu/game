#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "core/movement/MoveEntity.h"
#include "core/world/StaticWorld.h"

using Catch::Approx;
using rpg::MapKind;
using rpg::test::staticWorld;

TEST_CASE("Pacote de mundo: dois mapas, colisores e pontes", "[core][world]") {
  const auto& w = staticWorld();
  const auto& region = w.map(MapKind::Region);
  const auto& turb = w.map(MapKind::Turbulent);
  CHECK(w.worldLayoutVersion() == 2);
  CHECK(region.collision().size() + turb.collision().size() == 24753);
  CHECK(turb.collision().size() > 0);
  REQUIRE(region.terrain().bridges().size() == 2);
  CHECK(region.hasRouteField());
  CHECK_FALSE(turb.hasRouteField());
  CHECK(w.mapAtDemoX(0) == MapKind::Region);
  CHECK(w.mapAtDemoX(1400) == MapKind::Turbulent);
}

TEST_CASE("Ponte: no tabuleiro o chão é o piso; a rampa nunca afunda no relevo", "[core][world]") {
  const auto& region = staticWorld().map(MapKind::Region);
  for (const rpg::Bridge& b : region.terrain().bridges()) {
    INFO(b.name);
    for (double z = b.z - b.hd - b.apron - 2; z <= b.z + b.hd + b.apron + 2; z += 0.25) {
      for (double x = b.x - b.hw - b.apronSide - 2; x <= b.x + b.hw + b.apronSide + 2; x += 0.5) {
        const double ground = region.groundHeight(x, z), terrain = region.terrain().terrainHeight(x, z);
        CHECK(ground >= terrain);                                              // rampa por cima do relevo
        if (std::fabs(x - b.x) <= b.hw && std::fabs(z - b.z) <= b.hd) CHECK(ground >= b.y);  // tabuleiro
        if (std::fabs(z - b.z) > b.hd + b.apron || std::fabs(x - b.x) > b.hw + b.apronSide) CHECK(ground == terrain);
      }
    }
  }
}

TEST_CASE("Água: só no rio da região", "[core][world]") {
  const auto& w = staticWorld();
  const auto& region = w.map(MapKind::Region);
  const double x = 100;
  const double z = region.terrain().river().centerAt(x);
  CHECK(region.terrain().isWaterCell(x, z));
  CHECK(region.waterAt(x, z) == Approx(region.terrain().river().levelAt(x)));
  CHECK(region.waterAt(x, z) > region.terrain().terrainHeight(x, z));
  CHECK(w.map(MapKind::Turbulent).waterAt(1400, 0) == rpg::MapTerrain::kNoWater);
}

TEST_CASE("Zonas e lugares: entrepostos protegidos, Ermos em Full Loot, Turbulenta própria", "[core][world]") {
  const auto& w = staticWorld();
  const auto& z = w.zones().data();
  CHECK(w.zoneAt(MapKind::Region, z.mercado.x, z.mercado.z) == rpg::ZoneKind::Protegida);
  CHECK(w.placeAt(MapKind::Region, z.mercado.x, z.mercado.z) == rpg::PlaceId::Mercado);
  CHECK(w.zoneAt(MapKind::Region, z.ermos.x, z.ermos.z) == rpg::ZoneKind::FullLoot);
  CHECK(w.zoneAt(MapKind::Region, z.acampamento.x, z.acampamento.z) == rpg::ZoneKind::Fronteira);
  CHECK(w.zoneAt(MapKind::Turbulent, 1400, 0) == rpg::ZoneKind::Turbulenta);
  CHECK(w.placeAt(MapKind::Turbulent, 1400, 0) == rpg::PlaceId::Turbulenta);
  CHECK(rpg::placeKey(rpg::PlaceId::PonteMenor) == "ponteMenor");
  CHECK(rpg::placeFromKey("rioLargo") == rpg::PlaceId::RioLargo);
}

TEST_CASE("moveEntity: anda no plano, para no colisor e respeita o limite do mapa", "[core][movement]") {
  const auto& w = staticWorld();
  const auto& region = w.map(MapKind::Region);
  const auto& m = w.zones().data().mercado;
  rpg::XZ p{m.x, m.z};
  for (int i = 0; i < 30; ++i) rpg::moveEntity(region, p, 0.1, 0.0);
  CHECK((p.x != m.x || p.z != m.z));  // saiu do lugar (as bancas do mercado podem desviar o passo)

  // empurrado contra a borda do mapa, fica a 8 m dela
  rpg::XZ edge{500, 0};
  for (int i = 0; i < 200; ++i) rpg::moveEntity(region, edge, 1.0, 0.0);
  CHECK(edge.x <= 504.0);

  // na Turbulenta o limite é em volta do centro dela (x = 1.400)
  const auto& turb = w.map(MapKind::Turbulent);
  rpg::XZ t{1400, 0};
  for (int i = 0; i < 400; ++i) rpg::moveEntity(turb, t, 0.0, 1.0);
  CHECK(t.z <= 122.0);
  CHECK(t.x > 1270);
}
