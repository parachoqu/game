// Predição do próprio personagem (fase 8) contra o servidor de verdade, com latência em cada sentido: o
// passo previsto e refeito tem que coincidir com o que o servidor calculou. Com latência constante, cada
// comando chega a tempo do seu passo e a correção é nula; com variação, um passo sem comando novo repete
// o anterior no servidor e a correção fica do tamanho de um passo.
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "client/game/CommandBuilder.h"
#include "client/net/ClientSession.h"
#include "client/net/Predictor.h"
#include "net/LocalTransport.h"
#include "server/ServerHost.h"

using namespace rpg;
using namespace rpg::client;
namespace proto = rpg::protocol;
using rpg::test::gameData;
using rpg::test::staticWorld;

namespace {

struct Online {
  double clock = 0;
  net::LocalHub hub;
  server::ServerHost host;
  ClientSession session;
  explicit Online(net::NetSim ns)
      : hub(ns, [this] { return clock; }), host(gameData(), staticWorld(), devConfig(), hub.serverEndpoint()), session(hub.connectClient()) {}
  static server::ServerConfig devConfig() {
    server::ServerConfig c;
    c.devCommands = true;
    return c;
  }
  void tick() {
    clock += 1.0 / 30.0;
    host.runTicks(1);
    session.poll(clock);
  }
};

}  // namespace

namespace {

// Anda, corre, pula e vai de lado por 5 s; devolve a maior correção depois da primeira reconciliação.
double walkAround(Online& L, Predictor& pred, int& reconciles) {
  CommandBuilder cb;
  CommandContext cc;
  cc.camYaw = 0.7;
  const StaticMap& region = staticWorld().map(MapKind::Region);
  double worst = 0;
  std::uint32_t lastSeen = 0;
  for (int t = 0; t < 150; ++t) {
    InputState in;
    if (t < 110) in.keys.set(static_cast<std::size_t>(Key::W));
    if (t >= 40 && t < 70) in.keys.set(static_cast<std::size_t>(Key::LShift));
    if (t == 50 || t == 90) in.pressed.set(static_cast<std::size_t>(Key::Space));
    if (t >= 75 && t < 95) in.keys.set(static_cast<std::size_t>(Key::D));
    const proto::PlayerCommand cmd = cb.build(in, cc, false);
    L.session.send(cmd);
    pred.sent(cmd, 1.0 / 30.0);
    L.tick();
    pred.update(1.0 / 30.0);
    const proto::Snapshot* snap = L.session.snapshots().latest();
    if (!snap || !snap->self || snap->self->lastCommandSeq == lastSeen) continue;
    lastSeen = snap->self->lastCommandSeq;
    const proto::EntityState* me = nullptr;
    for (const auto& e : snap->entities)
      if (e.id == snap->self->playerId) me = &e;
    REQUIRE(me);
    pred.reconcile(*me, *snap->self, region);
    if (reconciles++ > 0) worst = std::max(worst, pred.lastError());
  }
  return worst;
}

}  // namespace

TEST_CASE("Predição: com variação na rede, a correção fica do tamanho de um passo", "[client][predict]") {
  Online L(net::NetSim{60, 40, 0});
  proto::Hello h;
  h.name = "Variado";
  L.session.join(h);
  for (int i = 0; i < 20 && !L.session.snapshots().latest(); ++i) L.tick();
  REQUIRE(L.session.state() == ClientSession::State::InGame);
  Predictor pred(gameData());
  int reconciles = 0;
  const double worst = walkAround(L, pred, reconciles);
  INFO("maior correção: " << worst << " m");
  CHECK(reconciles > 60);
  CHECK(worst < 0.6);  // correndo, ~0,2 m por passo repetido
}

TEST_CASE("Predição: andar, correr e pular previstos batem com o servidor (90 ms de cada lado)", "[client][predict]") {
  // 90 ms: cada comando chega antes do seu passo, sem cair em cima da virada do passo
  Online L(net::NetSim{90, 0, 0});
  proto::Hello h;
  h.name = "Previsto";
  h.start = "espadachim";
  L.session.join(h);
  for (int i = 0; i < 12 && !L.session.snapshots().latest(); ++i) L.tick();
  REQUIRE(L.session.state() == ClientSession::State::InGame);

  Predictor pred(gameData());
  int reconciles = 0;
  const double worst = walkAround(L, pred, reconciles);
  INFO("maior correção: " << worst << " m");
  CHECK(pred.active());
  CHECK(reconciles > 100);
  CHECK(worst < 0.05);
  // parado e confirmado: a previsão é a posição do servidor
  const proto::Snapshot* snap = L.session.snapshots().latest();
  REQUIRE(snap);
  const proto::EntityState* me = nullptr;
  for (const auto& e : snap->entities)
    if (e.id == snap->self->playerId) me = &e;
  REQUIRE(me);
  const glm::dvec3 p = pred.position();
  CHECK(std::hypot(p.x - me->x, p.z - me->z) < 0.01);
  CHECK(pred.pending() < 8);
}

TEST_CASE("Predição: teleporte entra direto e fora do estado livre vale o servidor", "[client][predict]") {
  Online L(net::NetSim{});
  proto::Hello h;
  h.name = "Teleporte";
  L.session.join(h);
  for (int i = 0; i < 6; ++i) L.tick();
  Predictor pred(gameData());
  CommandBuilder cb;
  const StaticMap& region = staticWorld().map(MapKind::Region);
  const auto sync = [&] {
    const proto::Snapshot* snap = L.session.snapshots().latest();
    REQUIRE(snap);
    for (const auto& e : snap->entities)
      if (e.id == snap->self->playerId) pred.reconcile(e, *snap->self, region);
  };
  for (int i = 0; i < 5; ++i) {
    const auto cmd = cb.build(InputState{}, CommandContext{}, false);
    L.session.send(cmd);
    pred.sent(cmd, 1.0 / 30.0);
    L.tick();
    sync();
  }
  REQUIRE(pred.active());
  L.session.send(proto::Request{proto::ReqDev{proto::DevCommand::Teleport, 40, 380, 0}});
  for (int i = 0; i < 3; ++i) {
    const auto cmd = cb.build(InputState{}, CommandContext{}, false);
    L.session.send(cmd);
    pred.sent(cmd, 1.0 / 30.0);
    L.tick();
    sync();
  }
  CHECK(std::hypot(pred.position().x - 40, pred.position().z - 380) < 0.5);  // sem deslizar até lá
  // derrubado: a predição desliga
  L.session.send(proto::Request{proto::ReqDev{proto::DevCommand::Hurt, 999, 0, 0}});
  for (int i = 0; i < 3; ++i) {
    const auto cmd = cb.build(InputState{}, CommandContext{}, false);
    L.session.send(cmd);
    pred.sent(cmd, 1.0 / 30.0);
    L.tick();
    sync();
  }
  CHECK_FALSE(pred.active());
}
