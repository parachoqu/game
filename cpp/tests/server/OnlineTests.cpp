// Dois clientes pela rede de verdade (ENet em 127.0.0.1) no mesmo mundo: entram, veem um ao outro, um
// anda e o outro vê. Os snapshots vão em delta e os comandos passam pelo validador.
#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "net/EnetTransport.h"
#include "server/CommandValidator.h"
#include "server/ServerHost.h"
#include "server/TestClient.h"

using rpg::test::gameData;
using rpg::test::staticWorld;
using rpg::test::TestClient;
namespace proto = rpg::protocol;

namespace {

const proto::EntityState* find(const proto::Snapshot& s, std::uint32_t id) {
  for (const auto& e : s.entities)
    if (e.id == id) return &e;
  return nullptr;
}

}  // namespace

TEST_CASE("Online: dois clientes por ENet entram, se veem e andam", "[server][online]") {
  auto server = std::make_unique<rpg::net::EnetServer>(0);
  const std::uint16_t port = server->port();
  rpg::server::ServerHost host(gameData(), staticWorld(), {}, std::move(server));
  TestClient a(std::make_unique<rpg::net::EnetClient>("127.0.0.1", port));
  TestClient b(std::make_unique<rpg::net::EnetClient>("127.0.0.1", port));
  const auto pump = [&](int ticks) {
    for (int i = 0; i < ticks; ++i) {
      host.runTicks(1);
      a.drain();
      b.drain();
      a.t->flush();
      b.t->flush();
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
  };
  a.hello("Ana");
  b.hello("Bia");
  for (int i = 0; i < 400 && (!a.welcome || !b.welcome || !a.snapshot || !b.snapshot); ++i) pump(1);
  REQUIRE(a.welcome);
  REQUIRE(b.welcome);
  CHECK(host.sessions().size() == 2);
  pump(10);
  REQUIRE(a.snapshot);
  REQUIRE(b.snapshot);
  // os dois nascem no abrigo: cada um vê o outro
  REQUIRE(find(*a.snapshot, b.welcome->playerId));
  REQUIRE(find(*b.snapshot, a.welcome->playerId));
  const double z0 = find(*b.snapshot, a.welcome->playerId)->z;

  // Ana anda para o norte por 1 s; Bia vê
  a.cmd.camYaw = 0;
  a.cmd.moveForward = 1;
  for (int i = 0; i < 30; ++i) {
    a.command();
    pump(1);
  }
  a.cmd.moveForward = 0;
  a.command();
  pump(15);
  const proto::EntityState* seen = find(*b.snapshot, a.welcome->playerId);
  REQUIRE(seen);
  CHECK(seen->z < z0 - 2.0);
  // os deltas chegaram (a maioria dos snapshots não é completa)
  CHECK(a.deltas > a.fullSnapshots);
}

TEST_CASE("Validador: movimento limitado, mira puxada para o alcance, taxa contida", "[server][validator]") {
  rpg::server::CommandValidator v;
  proto::PlayerCommand c;
  c.moveForward = 100;
  c.moveRight = -7;
  c.held = 0xFFFF;
  c.aimX = 10000;
  c.aimY = 0;
  c.aimZ = 0;
  c.camYaw = std::nan("");
  REQUIRE(v.command(c, 0, 0, 0, 0.0));
  CHECK(c.moveForward == 1);
  CHECK(c.moveRight == -1);
  CHECK(c.held == (proto::kHeldRun | proto::kHeldCrouch | proto::kHeldAim | proto::kHeldUiOpen));
  CHECK(std::abs(c.aimX - 300) < 1e-9);
  CHECK(c.camYaw == 0);
  CHECK(v.fixed() == 1);
  // 1000 comandos no mesmo instante: só a rajada passa
  int ok = 0;
  for (int i = 0; i < 1000; ++i) {
    proto::PlayerCommand x;
    ok += v.command(x, 0, 0, 0, 1.0) ? 1 : 0;
  }
  CHECK(ok < 100);
  CHECK(v.dropped() > 900);
  // pedidos: 30/s com rajada de 40
  int req = 0;
  for (int i = 0; i < 200; ++i) req += v.request(5.0) ? 1 : 0;
  CHECK(req == 40);
  CHECK(v.request(5.5));  // meio segundo depois, 15 fichas novas
}
