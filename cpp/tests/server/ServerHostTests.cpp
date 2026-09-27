#include <atomic>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "net/LocalTransport.h"
#include "server/ServerHost.h"

using rpg::test::gameData;
using rpg::test::staticWorld;

TEST_CASE("ServerHost roda passos fixos conforme o tempo real informado", "[server]") {
  rpg::server::ServerHost host(gameData(), staticWorld(), {.tickRate = 30.0}, nullptr);
  CHECK(host.update(0.02) == 0);
  CHECK(host.update(0.02) == 1);  // 0,04 s acumulados → 1 passo de 1/30
  host.runTicks(59);
  CHECK(host.world().tick() == 60);
}

TEST_CASE("ServerHost registra conexões e desconexões pelo transporte", "[server]") {
  rpg::net::LocalHub hub;
  rpg::server::ServerHost host(gameData(), staticWorld(), {}, hub.serverEndpoint());
  auto a = hub.connectClient();
  auto b = hub.connectClient();
  host.runTicks(1);
  CHECK(host.sessions().size() == 2);

  a->send(rpg::net::kServer, rpg::net::Channel::Commands, {});
  a.reset();
  host.runTicks(1);
  CHECK(host.sessions().size() == 1);
  CHECK(host.ignoredMessages() == 1);  // protocolo ainda não existe: a mensagem é só contada
}

TEST_CASE("ServerHost roda numa thread própria e para quando pedido", "[server]") {
  rpg::server::ServerHost host(gameData(), staticWorld(), {.tickRate = 200.0}, nullptr);
  std::atomic<bool> stop{false};
  std::thread t([&] { host.run(stop); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // ~20 passos a 200 Hz
  stop = true;
  t.join();
  CHECK(host.world().tick() >= 5);
}
