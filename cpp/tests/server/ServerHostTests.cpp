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
  rpg::server::ServerConfig cfg;
  cfg.tickRate = 30.0;
  rpg::server::ServerHost host(gameData(), staticWorld(), cfg, nullptr);
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
  rpg::server::ServerConfig cfg;
  cfg.tickRate = 200.0;
  rpg::server::ServerHost host(gameData(), staticWorld(), cfg, nullptr);
  std::atomic<bool> stop{false};
  std::thread t([&] { host.run(stop); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // ~20 passos a 200 Hz
  stop = true;
  t.join();
  CHECK(host.world().tick() >= 5);
}

#include "core/protocol/Session.h"
#include "core/protocol/SnapshotDelta.h"
#include "net/Bytes.h"

namespace {

// Lê tudo o que o servidor mandou ao cliente: mensagens de evento e o último snapshot.
struct Inbox {
  std::vector<rpg::protocol::ServerMessage> events;
  std::optional<rpg::protocol::Snapshot> snapshot;
  rpg::protocol::DeltaDecoder decoder;
  int snapshots = 0;
  void drain(rpg::net::ITransport& t) {
    rpg::net::Message m;
    while (t.poll(m)) {
      if (m.kind != rpg::net::Message::Kind::Data) continue;
      if (m.channel == rpg::net::Channel::Events) {
        auto e = rpg::protocol::decode<rpg::protocol::ServerMessage>(rpg::net::asU8(m.bytes));
        REQUIRE(e);
        events.push_back(std::move(*e));
      } else if (m.channel == rpg::net::Channel::Snapshots) {
        auto d = rpg::protocol::decode<rpg::protocol::SnapshotDelta>(rpg::net::asU8(m.bytes));
        REQUIRE(d);
        auto s = decoder.decode(*d);
        REQUIRE(s);
        snapshot = std::move(*s);
        ++snapshots;
      }
    }
  }
};

void sendMsg(rpg::net::ITransport& t, rpg::net::Channel ch, const rpg::protocol::Bytes& b) {
  t.send(rpg::net::kServer, ch, rpg::net::asBytes(b));
}

}  // namespace

TEST_CASE("ServerHost: Hello cria o personagem, comandos movem e snapshots chegam", "[server][protocol]") {
  namespace proto = rpg::protocol;
  rpg::net::LocalHub hub;
  rpg::server::ServerHost host(gameData(), staticWorld(), {}, hub.serverEndpoint());
  auto client = hub.connectClient();
  proto::Hello hello;
  hello.name = "Ilo";
  hello.origin = "anao";
  hello.start = "artesao";
  sendMsg(*client, rpg::net::Channel::Requests, proto::encode(proto::ClientMessage{hello}));
  host.runTicks(1);
  Inbox in;
  in.drain(*client);
  REQUIRE(!in.events.empty());
  REQUIRE(std::holds_alternative<proto::Welcome>(in.events[0]));
  const std::uint32_t me = std::get<proto::Welcome>(in.events[0]).playerId;
  REQUIRE(in.snapshot);
  REQUIRE(in.snapshot->self);
  CHECK(in.snapshot->self->playerId == me);
  const double z0 = in.snapshot->self ? in.snapshot->entities[0].z : 0;

  // anda para o norte (câmera em 0: W vai para −z) por um segundo
  proto::PlayerCommand cmd;
  for (int i = 0; i < 30; ++i) {
    cmd.seq++;
    cmd.moveForward = 1;
    sendMsg(*client, rpg::net::Channel::Commands, proto::encode(cmd));
    host.runTicks(1);
  }
  in.drain(*client);
  const proto::EntityState* self = nullptr;
  for (const auto& e : in.snapshot->entities)
    if (e.id == me) self = &e;
  REQUIRE(self);
  CHECK(self->z < z0 - 2.0);  // ~3 m/s andando
  CHECK(in.snapshot->self->lastCommandSeq == cmd.seq);

  // pedido: vender sem estar no mercado é recusado com aviso
  sendMsg(*client, rpg::net::Channel::Requests, proto::encode(proto::ClientMessage{proto::Request{proto::ReqSell{0, 0, 1, false}}}));
  host.runTicks(1);
  in.events.clear();
  in.drain(*client);
  bool sawToast = false;
  for (const auto& e : in.events)
    if (const auto* ge = std::get_if<proto::GameEvent>(&e); ge && std::holds_alternative<proto::EvToast>(*ge)) sawToast = true;
  CHECK(sawToast);
}

TEST_CASE("ServerHost: versão de protocolo diferente é recusada", "[server][protocol]") {
  namespace proto = rpg::protocol;
  rpg::net::LocalHub hub;
  rpg::server::ServerHost host(gameData(), staticWorld(), {}, hub.serverEndpoint());
  auto client = hub.connectClient();
  proto::Hello hello;
  hello.protocol = 999;
  sendMsg(*client, rpg::net::Channel::Requests, proto::encode(proto::ClientMessage{hello}));
  host.runTicks(1);
  Inbox in;
  in.drain(*client);
  REQUIRE(in.events.size() == 1);
  CHECK(std::holds_alternative<proto::Reject>(in.events[0]));
  CHECK(host.sessions().front().player == 0);
}

TEST_CASE("ServerHost: bots jogam sozinhos por um minuto de jogo sem quebrar nada", "[server][bots]") {
  rpg::server::ServerConfig cfg;
  cfg.bots = 4;
  cfg.seed = 7;
  rpg::server::ServerHost host(gameData(), staticWorld(), cfg, nullptr);
  host.runTicks(30 * 60);
  CHECK(host.botCount() == 4);
  CHECK(host.world().state().players.size() == 4);
}
