#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "core/protocol/Requests.h"
#include "sim/Sim.h"
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

namespace {

const rpg::protocol::EntityState* entityOf(const rpg::protocol::Snapshot& s, rpg::sim::EntityId id) {
  for (const auto& e : s.entities)
    if (e.id == id) return &e;
  return nullptr;
}

}  // namespace

TEST_CASE("Arma na mão: some ao equipar inutilizada, segue à mostra se quebrar em uso, volta no reparo", "[sim][world]") {
  rpg::sim::World w(gameData(), staticWorld(), rpg::sim::WorldConfig{7, {}, false});
  const rpg::sim::EntityId id = w.addPlayer({"Arma", "humano", "espadachim", "kachujin"});
  rpg::sim::Player& p = *w.state().player(id);
  REQUIRE(p.equip.weapon);
  const auto weaponOf = [&] { return entityOf(w.snapshot(id), id)->weapon; };
  const std::uint16_t sword = weaponOf();
  CHECK(sword != 0xFFFF);

  // quebrou em combate: a demo não troca o modelo, a espada continua na mão
  p.equip.weapon->cond = 0;
  CHECK(weaponOf() == sword);

  // reequipar a inutilizada: mãos vazias (panels.js: setWeapon(e.cond > 0 ? … : null))
  w.handleRequest(id, rpg::protocol::ReqUnequip{0});
  REQUIRE(!p.inv.empty());
  w.handleRequest(id, rpg::protocol::ReqEquip{static_cast<std::uint32_t>(p.inv.size() - 1)});
  REQUIRE(p.equip.weapon);
  CHECK(weaponOf() == 0xFFFF);

  // reaparecer com ela ainda inutilizada: continua sem arma; o reparo devolve
  p.state = rpg::sim::PlayerState::Dead;
  w.handleRequest(id, rpg::protocol::ReqRespawn{});
  CHECK(weaponOf() == 0xFFFF);
  p.coins = 500;
  p.inv.push_back({rpg::sim::item(w.state(), "lingote"), 1});  // inutilizada: o reparo pede um lingote
  rpg::sim::repair(w.state(), p, rpg::sim::GearSlot::Weapon);
  CHECK(p.equip.weapon->cond == 100);
  CHECK(weaponOf() == sword);
}
