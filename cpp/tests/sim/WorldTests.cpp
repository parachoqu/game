#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "sim/World.h"

using Catch::Approx;
using rpg::test::gameData;
using rpg::test::staticWorld;

TEST_CASE("World avança em passo fixo e o relógio segue o config.js", "[sim][world]") {
  rpg::sim::World w(gameData(), staticWorld(), rpg::sim::WorldConfig{1, {}, false});
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
  rpg::sim::World a(gameData(), staticWorld(), rpg::sim::WorldConfig{42, {}, false});
  rpg::sim::World b(gameData(), staticWorld(), rpg::sim::WorldConfig{42, {}, false});
  for (int i = 0; i < 10; ++i) CHECK(a.state().rng->next() == b.state().rng->next());
}
