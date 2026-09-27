#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/data/GameData.h"
#include "sim/World.h"

using Catch::Approx;

namespace {
const rpg::GameData& data() {
  static const rpg::GameData d = rpg::GameData::load(RPG_TEST_DATA_DIR);
  return d;
}
}  // namespace

TEST_CASE("World avança em passo fixo e o relógio segue o config.js", "[sim][world]") {
  rpg::sim::World w(data(), 1);
  CHECK(w.tick() == 0);
  CHECK(w.clock().day == 1);
  CHECK(w.clock().hour == Approx(9.0));

  const double dt = 1.0 / 30.0;
  for (int i = 0; i < 30 * 420; ++i) w.step(dt);  // um dia inteiro de jogo (420 s)
  CHECK(w.tick() == 30 * 420);
  CHECK(w.time() == Approx(420.0));
  CHECK(w.clock().day == 2);
  CHECK(w.clock().hour == Approx(9.0).margin(1e-6));
}

TEST_CASE("World: mesma semente, mesma sequência aleatória", "[sim][world]") {
  rpg::sim::World a(data(), 42), b(data(), 42);
  for (int i = 0; i < 10; ++i) CHECK(a.rng().nextU32() == b.rng().nextU32());
}
