#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "net/LocalTransport.h"

using namespace rpg::net;

namespace {

struct ManualClock {
  double t = 100.0;
  TimeSource source() {
    return [this] { return t; };
  }
};

std::vector<std::byte> bytes(const std::string& s) {
  std::vector<std::byte> out(s.size());
  std::memcpy(out.data(), s.data(), s.size());
  return out;
}

std::string text(const Message& m) { return {reinterpret_cast<const char*>(m.bytes.data()), m.bytes.size()}; }

std::vector<std::string> drainData(ITransport& t) {
  std::vector<std::string> out;
  Message m;
  while (t.poll(m))
    if (m.kind == Message::Kind::Data) out.push_back(text(m));
  return out;
}

}  // namespace

TEST_CASE("LocalTransport: conexão, ida e volta e desconexão", "[net]") {
  LocalHub hub;
  auto server = hub.serverEndpoint();
  auto client = hub.connectClient();

  Message m;
  REQUIRE(server->poll(m));
  CHECK(m.kind == Message::Kind::Connected);
  const ConnectionId id = m.peer;
  CHECK(id != kServer);

  client->send(kServer, Channel::Requests, bytes("comprar espada"));
  REQUIRE(server->poll(m));
  CHECK(m.kind == Message::Kind::Data);
  CHECK(m.peer == id);
  CHECK(m.channel == Channel::Requests);
  CHECK(text(m) == "comprar espada");

  server->send(id, Channel::Events, bytes("comprou"));
  REQUIRE(client->poll(m));
  CHECK(m.peer == kServer);
  CHECK(text(m) == "comprou");

  client.reset();
  REQUIRE(server->poll(m));
  CHECK(m.kind == Message::Kind::Disconnected);
  CHECK(m.peer == id);
}

TEST_CASE("LocalTransport: vários clientes com ids distintos", "[net]") {
  LocalHub hub;
  auto server = hub.serverEndpoint();
  auto a = hub.connectClient();
  auto b = hub.connectClient();
  Message m;
  REQUIRE(server->poll(m));
  const auto ida = m.peer;
  REQUIRE(server->poll(m));
  const auto idb = m.peer;
  CHECK(ida != idb);
  server->send(idb, Channel::Snapshots, bytes("só para b"));
  CHECK(drainData(*a).empty());
  CHECK(drainData(*b) == std::vector<std::string>{"só para b"});
}

TEST_CASE("LocalTransport: latência segura a entrega até a hora certa", "[net]") {
  ManualClock clock;
  LocalHub hub(NetSim{.latencyMs = 100}, clock.source());
  auto server = hub.serverEndpoint();
  auto client = hub.connectClient();
  client->send(kServer, Channel::Commands, bytes("w"));

  Message m;
  CHECK_FALSE(server->poll(m));  // nem o Connected chegou ainda
  clock.t += 0.099;
  CHECK_FALSE(server->poll(m));
  clock.t += 0.002;
  REQUIRE(server->poll(m));
  CHECK(m.kind == Message::Kind::Connected);
  REQUIRE(server->poll(m));
  CHECK(text(m) == "w");
}

TEST_CASE("LocalTransport: canais confiáveis mantêm a ordem mesmo com jitter", "[net]") {
  ManualClock clock;
  LocalHub hub(NetSim{.latencyMs = 20, .jitterMs = 200, .seed = 99}, clock.source());
  auto server = hub.serverEndpoint();
  auto client = hub.connectClient();
  std::vector<std::string> sent;
  for (int i = 0; i < 50; ++i) {
    sent.push_back(std::to_string(i));
    client->send(kServer, Channel::Requests, bytes(sent.back()));
    clock.t += 0.001;
  }
  clock.t += 1.0;
  CHECK(drainData(*server) == sent);
}

TEST_CASE("LocalTransport: perda afeta só os canais não confiáveis", "[net]") {
  ManualClock clock;
  LocalHub hub(NetSim{.lossPercent = 100}, clock.source());
  auto server = hub.serverEndpoint();
  auto client = hub.connectClient();
  client->send(kServer, Channel::Commands, bytes("perdido"));
  client->send(kServer, Channel::Snapshots, bytes("perdido"));
  client->send(kServer, Channel::Requests, bytes("chega"));
  client->send(kServer, Channel::Events, bytes("chega também"));
  CHECK(drainData(*server) == std::vector<std::string>{"chega", "chega também"});
}

TEST_CASE("LocalTransport: servidor encerrado avisa os clientes", "[net]") {
  LocalHub hub;
  auto server = hub.serverEndpoint();
  auto client = hub.connectClient();
  server.reset();
  Message m;
  REQUIRE(client->poll(m));
  CHECK(m.kind == Message::Kind::Disconnected);
  CHECK(m.peer == kServer);
}
