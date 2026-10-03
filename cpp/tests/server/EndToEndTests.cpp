// Uma trajetória inteira pelo protocolo, como o cliente joga: só bytes pelo LocalTransport, com os
// comandos de desenvolvimento no lugar da caminhada longa (window.__demo da demo). No fim, o personagem
// sai, é salvo em arquivo, volta com "Continuar" e precisa estar igual.
#include <algorithm>
#include <cmath>
#include <functional>

#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "server/ServerHost.h"
#include "server/TestClient.h"
#include "server/persist/SaveStore.h"
#include "sim/Sim.h"

namespace proto = rpg::protocol;
using rpg::test::gameData;
using rpg::test::staticWorld;
using rpg::test::TempDir;
using rpg::test::TestClient;

namespace {

const rpg::LayoutInteractable& service(std::string_view kind, std::string_view data = {}) {
  const auto& all = staticWorld().layout().interactables;
  const auto it = std::find_if(all.begin(), all.end(),
                               [&](const auto& i) { return i.kind == kind && (data.empty() || i.data == data); });
  REQUIRE(it != all.end());
  return *it;
}

}  // namespace

TEST_CASE("Ponta a ponta: comprar, coletar, fabricar, cair na Fronteira, recuperar a carga, portal, extração e retomar o save",
          "[e2e][server]") {
  const rpg::GameData& D = gameData();
  const auto item = [&](const char* k) { return D.items.find(k); };
  TempDir dir("e2e");
  rpg::server::ServerConfig cfg;
  cfg.saveDir = dir.path;
  cfg.devCommands = true;
  cfg.autosaveEvery = 1e9;  // só os saves pedidos
  rpg::net::LocalHub hub;
  rpg::server::ServerHost host(D, staticWorld(), cfg, hub.serverEndpoint());
  TestClient c(hub.connectClient());
  host.runTicks(1);

  c.hello("Bruna", "artesao");
  host.runTicks(2);
  c.drain();
  REQUIRE(c.welcome);
  const std::uint32_t me = c.welcome->playerId;
  const rpg::sim::Sim& S = host.world().state();
  const auto P = [&]() -> const rpg::sim::Player& { return *S.player(me); };
  const auto run = [&](double seconds) {
    host.runTicks(static_cast<std::uint64_t>(std::ceil(seconds * 30)));
    c.drain();
  };
  const auto runUntil = [&](double maxSeconds, const std::function<bool()>& done) {
    for (int i = 0; i < static_cast<int>(maxSeconds * 30) && !done(); ++i) host.runTicks(1);
    c.drain();
    return done();
  };
  const auto tp = [&](double x, double z) {
    c.dev(proto::DevCommand::Teleport, x, z);
    run(0.1);
  };
  const auto give = [&](const char* k, int n) {
    c.dev(proto::DevCommand::Give, n, 0, item(k).value);
    run(0.05);
  };
  const auto inv = [&](const char* k) { return rpg::sim::count(P().inv, item(k)); };
  c.command();  // o primeiro comando só estabelece os contadores de toques
  run(0.1);

  // ---- comprar no mercado do Vale
  const auto& market = service("market", "vale");
  tp(market.x, market.z);
  const int potions = inv("pocao");
  const double coins = P().coins;
  c.request(proto::ReqBuy{D.markets.find("vale").value, item("pocao").value, 2});
  run(0.2);
  CHECK(inv("pocao") == potions + 2);
  CHECK(P().coins < coins);

  // ---- coletar: F perto do nó de madeira do bosque (cenário "coleta" da demo)
  const auto& node = staticWorld().layout().nodes.front();
  REQUIRE(node.kind == "madeira");
  tp(node.x + 1.2, node.z);
  const int wood = inv("madeira");
  c.press(proto::Press::Interact);
  CHECK(runUntil(8, [&] { return inv("madeira") > wood; }));

  // ---- fabricar um lingote na forja
  give("minerio", 2);
  const auto& forge = service("forge");
  tp(forge.x, forge.z);
  const int ingots = inv("lingote");
  c.request(proto::ReqCraft{0, false});
  run(0.2);
  CHECK(inv("lingote") == ingots + 1);
  CHECK(rpg::sim::statOf(P(), "crafted") == 1);

  // ---- a Passagem (Fronteira): descoberta, derrota, a carga fica num saco
  tp(-25, -50);
  run(1);
  CHECK(P().zone == rpg::ZoneKind::Fronteira);
  CHECK(P().flags.contains("disc_passagem"));
  give("pele", 3);
  const int hides = inv("pele");
  const int carried = static_cast<int>(P().inv.size());
  REQUIRE(carried > 0);
  c.dev(proto::DevCommand::Hurt, 999);
  REQUIRE(runUntil(40, [&] { return P().state == rpg::sim::PlayerState::Dead; }));
  const auto died = c.gameEvents<proto::EvPlayerDied>();
  REQUIRE(died.size() == 1);
  CHECK(died[0].report.zone == static_cast<std::uint8_t>(rpg::ZoneKind::Fronteira));
  CHECK(died[0].report.bag);
  CHECK(P().inv.empty());
  const rpg::sim::LootBag* bag = nullptr;
  for (const auto& b : S.lootBags)
    if (b->owner == me) bag = b.get();
  REQUIRE(bag);
  const rpg::sim::EntityId bagId = bag->id;
  const double bx = bag->x, bz = bag->z;

  c.request(proto::ReqRespawn{});
  run(0.5);
  CHECK(P().state == rpg::sim::PlayerState::Free);
  CHECK(std::hypot(P().pos.x - S.layout.shelter.x, P().pos.z - S.layout.shelter.z) < 10);
  CHECK(c.gameEvents<proto::EvRespawned>().size() == 1);

  // ---- recuperar a carga
  tp(bx, bz);
  c.request(proto::ReqTakeLoot{bagId, -1});
  run(0.3);
  CHECK(inv("pele") == hides);
  CHECK(std::none_of(S.lootBags.begin(), S.lootBags.end(), [&](const auto& b) { return b->id == bagId; }));

  // ---- portal, incursão e extração
  give("fragmento", 1);
  c.dev(proto::DevCommand::Portal);
  run(0.1);
  REQUIRE(S.turb.portal);
  tp(S.turb.portal->x - 2, S.turb.portal->z);
  c.request(proto::ReqEnterPortal{});
  run(0.5);
  REQUIRE(P().turb.inside);
  CHECK(P().map == rpg::MapKind::Turbulent);
  CHECK(c.gameEvents<proto::EvTurbEnter>().size() == 1);
  REQUIRE_FALSE(P().turb.exits.empty());
  const auto exit = P().turb.exits.front();
  tp(exit.x, exit.z);
  c.press(proto::Press::Interact);
  REQUIRE(runUntil(15, [&] { return !P().turb.inside; }));
  CHECK(P().map == rpg::MapKind::Region);
  const auto left = c.gameEvents<proto::EvTurbLeave>();
  REQUIRE(left.size() == 1);
  CHECK(left[0].success);

  // ---- sair, salvar e voltar com "Continuar"
  const rpg::sim::Player before = P();
  c.request(proto::ReqLeave{false});
  run(0.2);
  REQUIRE(host.store()->hasCharacter("Bruna"));
  c.hello("Bruna", "artesao", true);
  run(0.2);
  REQUIRE(c.welcome);
  CHECK(c.welcome->resumed);
  const rpg::sim::Player& after = *S.player(c.welcome->playerId);
  CHECK(after.coins == before.coins);
  CHECK(after.trainings == before.trainings);
  for (const char* k : {"pocao", "madeira", "lingote", "pele", "minerio", "fragmento"})
    CHECK(rpg::sim::count(after.inv, item(k)) == rpg::sim::count(before.inv, item(k)));
  CHECK(after.stats == before.stats);
  CHECK(after.flags == before.flags);
  REQUIRE(after.book.entries().size() == before.book.entries().size());
  for (std::size_t i = 0; i < before.book.entries().size(); ++i) {
    CHECK(after.book.entries()[i].key == before.book.entries()[i].key);
    CHECK(after.book.entries()[i].count == before.book.entries()[i].count);
  }
  CHECK(std::hypot(after.pos.x - before.pos.x, after.pos.z - before.pos.z) < 0.05);
  REQUIRE(after.equip.weapon);
  CHECK(after.equip.weapon->cond == before.equip.weapon->cond);
  // o Livro inteiro chega ao cliente na volta
  CHECK(c.gameEvents<proto::EvBookSync>().back().entries.size() == before.book.entries().size());
}
