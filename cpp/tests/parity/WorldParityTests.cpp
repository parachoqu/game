// Paridade com a demo JS: cada consulta do C++ tem que dar exatamente o mesmo resultado que o JS deu
// para as mesmas entradas (fixtures geradas por `node demo/tools/bake-sim-world.mjs`).
//
// As comparações são exatas (==), não aproximadas: as contas foram portadas na mesma ordem, com o
// Math.hypot e o Math.round do V8, e o build desliga a fusão de multiplicação+soma.
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "TestData.h"
#include "core/Noise.h"
#include "core/movement/MoveEntity.h"
#include "core/world/StaticWorld.h"

using nlohmann::json;
using rpg::MapKind;
using rpg::test::staticWorld;

namespace {

const json& fixtures() {
  static const json j = [] {
    std::ifstream in(std::string(RPG_TEST_FIXTURES_DIR) + "/world-parity.json");
    REQUIRE(in.good());
    return json::parse(in);
  }();
  return j;
}

const json& placeNames() {
  static const json j = [] {
    std::ifstream in(std::string(RPG_TEST_DATA_DIR) + "/text/pt-BR/places.json");
    REQUIRE(in.good());
    return json::parse(in);
  }();
  return j;
}

// Mismatch acumulado: reporta os primeiros casos e o total, em vez de milhares de CHECKs.
struct Mismatches {
  int count = 0;
  std::ostringstream first;
  template <class A, class B>
  void expect(bool ok, const std::string& what, const A& got, const B& want) {
    if (ok) return;
    if (++count <= 8) first << what << ": C++ " << got << " ≠ JS " << want << "\n";
  }
};

std::string repr(double v) {
  std::ostringstream s;
  s.precision(17);
  s << v;
  return s.str();
}

}  // namespace

TEST_CASE("Paridade: relevo, pontes, água, rotas, biomas, zonas e lugares", "[parity][world]") {
  const auto& w = staticWorld();
  Mismatches m;
  const auto& samples = fixtures().at("terrain");
  REQUIRE(samples.size() > 2000);
  for (const auto& s : samples) {
    const double x = s.at("x"), z = s.at("z");
    const MapKind map = w.mapAtDemoX(x);
    const auto& sm = w.map(map);
    const std::string at = "(" + repr(x) + ", " + repr(z) + ")";

    const double terrain = sm.terrain().terrainHeight(x, z);
    const double ground = sm.groundHeight(x, z);
    m.expect(terrain == s.at("terrain").get<double>(), "terrainHeight" + at, repr(terrain), s.at("terrain"));
    m.expect(ground == s.at("ground").get<double>(), "groundHeight" + at, repr(ground), s.at("ground"));

    const double water = sm.waterAt(x, z);
    if (s.at("water").is_null()) m.expect(std::isinf(water) && water < 0, "waterAt" + at, repr(water), "-∞");
    else m.expect(water == s.at("water").get<double>(), "waterAt" + at, repr(water), s.at("water"));

    const std::string zone(rpg::zoneKey(w.zoneAt(map, x, z)));
    m.expect(zone == s.at("zone").get<std::string>(), "zoneAt" + at, zone, s.at("zone"));
    const std::string placeKey(rpg::placeKey(w.placeAt(map, x, z)));
    const std::string place = placeNames().at(placeKey).at("name");
    m.expect(place == s.at("place").get<std::string>(), "placeAt" + at, place, s.at("place"));

    if (map == MapKind::Region) {
      const double route = sm.routeDistance(x, z);
      m.expect(route == s.at("route").get<double>(), "routeDistance" + at, repr(route), s.at("route"));
      const int biome = sm.terrain().biomeAt(x, z);
      m.expect(biome == s.at("biome").get<int>(), "biomeAt" + at, biome, s.at("biome"));
      const bool wet = sm.terrain().isWaterCell(x, z);
      m.expect(wet == s.at("wet").get<bool>(), "isWaterCell" + at, wet, s.at("wet"));
      const double slope = sm.terrain().slopeAt(x, z);
      m.expect(slope == s.at("slope").get<double>(), "slopeAt" + at, repr(slope), s.at("slope"));
    }
  }
  INFO(m.first.str());
  CHECK(m.count == 0);
}

TEST_CASE("Paridade: testPoint e resolve contra os 24.753 colisores", "[parity][collision]") {
  const auto& w = staticWorld();
  Mismatches m;
  const auto& samples = fixtures().at("collision");
  REQUIRE(samples.size() > 2000);
  int hits = 0;
  for (const auto& s : samples) {
    const double x = s.at("x"), z = s.at("z"), r = s.at("r");
    const auto& grid = w.map(w.mapAtDemoX(x)).collision();
    const std::string at = "(" + repr(x) + ", " + repr(z) + ", r=" + repr(r) + ")";
    const bool hit = grid.testPoint(x, z, r);
    hits += hit ? 1 : 0;
    m.expect(hit == s.at("hit").get<bool>(), "testPoint" + at, hit, s.at("hit"));
    const rpg::XZ p = grid.resolve(x, z, r);
    m.expect(p.x == s.at("rx").get<double>() && p.z == s.at("rz").get<double>(), "resolve" + at,
             repr(p.x) + "," + repr(p.z), s.at("rx").dump() + "," + s.at("rz").dump());
  }
  INFO(m.first.str());
  CHECK(m.count == 0);
  CHECK(hits > 100);  // as amostras exercitam colisões de verdade, não só chão livre
}

TEST_CASE("Paridade: moveEntity passo a passo (encostas, estradas, rio, pontes, limites, Turbulenta)", "[parity][movement]") {
  const auto& w = staticWorld();
  Mismatches m;
  const auto& walkers = fixtures().at("movement");
  REQUIRE(walkers.size() > 50);
  int steps = 0, deviated = 0;
  for (const auto& wk : walkers) {
    rpg::XZ p{wk.at("x").get<double>(), wk.at("z").get<double>()};
    const auto& map = w.map(w.mapAtDemoX(p.x));
    const double r = wk.at("r");
    for (const auto& st : wk.at("steps")) {
      const double dx = st.at("dx"), dz = st.at("dz");
      const rpg::XZ free{p.x + dx, p.z + dz};
      rpg::moveEntity(map, p, dx, dz, r, st.at("lift"));
      ++steps;
      if (!(p == free)) ++deviated;  // encosta, colisor ou limite mudou o passo
      const bool ok = p.x == st.at("x").get<double>() && p.z == st.at("z").get<double>();
      m.expect(ok, "moveEntity passo " + std::to_string(steps), repr(p.x) + "," + repr(p.z),
               st.at("x").dump() + "," + st.at("z").dump());
      if (!ok) p = {st.at("x").get<double>(), st.at("z").get<double>()};  // segue do valor do JS
    }
  }
  INFO(m.first.str());
  CHECK(m.count == 0);
  CHECK(steps > 3000);
  CHECK(deviated > 100);  // as amostras exercitam encostas, colisores e limites, não só chão livre
}

TEST_CASE("Paridade: ruído determinístico (hash2, vnoise, fbm2)", "[parity][noise]") {
  Mismatches m;
  for (const auto& s : fixtures().at("noise")) {
    const double x = s.at("x"), z = s.at("z"), seed = s.at("seed");
    const double h = rpg::hash2(std::floor(x), std::floor(z), seed);
    const double v = rpg::vnoise(x, z, seed);
    const double f = rpg::fbm2(x * 0.01, z * 0.01, 3, seed);
    m.expect(h == s.at("hash").get<double>(), "hash2", repr(h), s.at("hash"));
    m.expect(v == s.at("v").get<double>(), "vnoise", repr(v), s.at("v"));
    m.expect(f == s.at("fbm").get<double>(), "fbm2", repr(f), s.at("fbm"));
  }
  INFO(m.first.str());
  CHECK(m.count == 0);
}
