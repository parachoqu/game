// Cliente sem janela: interpolação, sessão contra o servidor de verdade (LocalTransport), textos,
// alvos, câmera, mira, malha do relevo e PNG. Nada aqui precisa de GPU.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "client/ClientWorld.h"
#include "client/game/Aim.h"
#include "client/game/CommandBuilder.h"
#include "client/game/Lighting.h"
#include "client/game/Targets.h"
#include "client/game/ThirdPersonCamera.h"
#include "client/geom/SceneBatch.h"
#include "client/geom/TerrainMesh.h"
#include "client/image/PngWriter.h"
#include "client/net/ClientSession.h"
#include "client/ui/Localization.h"
#include "core/Paths.h"
#include "core/protocol/Replicated.h"
#include "net/LocalTransport.h"
#include "server/ServerHost.h"

using namespace rpg;
using namespace rpg::client;
using Catch::Approx;
using rpg::test::gameData;
using rpg::test::staticWorld;
namespace proto = rpg::protocol;

namespace {

const Localization& loc() {
  static const Localization L = Localization::load(pathFromUtf8(RPG_TEST_DATA_DIR) / "text" / "pt-BR", gameData());
  return L;
}

proto::Snapshot snapAt(std::uint64_t tick, double x, double yaw = 0) {
  proto::Snapshot s;
  s.world.tick = tick;
  proto::EntityState e;
  e.id = 7;
  e.x = x;
  e.yaw = yaw;
  s.entities.push_back(e);
  return s;
}

// Cliente e servidor no mesmo processo, avançados à mão (sem threads nem relógio real).
struct Loop {
  net::LocalHub hub{net::NetSim{}, [this] { return clock; }};
  double clock = 0;
  server::ServerHost host;
  ClientSession session;
  ClientWorld world;

  explicit Loop(server::ServerConfig cfg = {})
      : host(gameData(), staticWorld(), cfg, hub.serverEndpoint()), session(hub.connectClient()) {}

  void step(int ticks = 1) {
    for (int i = 0; i < ticks; ++i) {
      clock += 1.0 / 30.0;
      host.runTicks(1);
      session.poll(clock);
    }
    world.update(session, clock + 1.0);  // bem à frente: desenha o snapshot mais novo
  }

  void join() {
    proto::Hello h;
    h.name = "Teste";
    h.origin = "humano";
    h.start = "espadachim";
    h.model = "kachujin";
    session.join(h);
    step(3);
  }
};

}  // namespace

TEST_CASE("Interpolação de snapshots: meio do caminho, ângulo pelo lado curto, teleporte sem deslizar", "[client]") {
  SnapshotInterpolator si(30.0, 1.0);
  si.push(snapAt(10, 0.0, 3.0), 0.0);
  si.push(snapAt(11, 1.0, -3.0), 1.0 / 30.0);
  // desenha 1 passo atrás do servidor: no instante do tick 10,5 → x = 0,5
  const double local = 1.0 / 30.0 + 0.5 / 30.0;
  auto e = si.sample(local);
  REQUIRE(e.size() == 1);
  CHECK(e[0].x == Approx(0.5).margin(0.05));
  // 3 → −3 rad passa por ±π, não por 0
  CHECK(std::abs(std::abs(e[0].yaw) - 3.14159) < 0.2);

  const auto t = lerpEntity(snapAt(1, 0.0).entities[0], snapAt(2, 50.0).entities[0], 0.5);
  CHECK(t.x == 50.0);

  si.push(snapAt(5, 9.0), 2.0);  // chegou fora de ordem: descartado
  CHECK(si.dropped() == 1);
}

TEST_CASE("Sessão do cliente entra no mundo, anda com comandos e reaparece depois da derrota", "[client]") {
  server::ServerConfig cfg;
  cfg.devCommands = true;
  Loop L(cfg);
  L.join();
  REQUIRE(L.session.state() == ClientSession::State::InGame);
  REQUIRE(L.world.ready());
  const proto::EntityState start = *L.world.me();
  CHECK(start.kind == proto::EntityKind::Player);
  CHECK(L.world.self()->hp > 0);

  // W por 1 s com a câmera olhando para o norte (yaw 0 → −z)
  CommandBuilder cb;
  InputState in;
  in.keys.set(static_cast<std::size_t>(Key::W));
  CommandContext cc;
  for (int i = 0; i < 30; ++i) {
    L.session.send(cb.build(in, cc, false));
    L.step();
  }
  const proto::EntityState* me = L.world.me();
  REQUIRE(me);
  CHECK(me->z < start.z - 1.5);
  CHECK(std::abs(me->x - start.x) < 0.5);

  // derrota por comando de desenvolvimento → relatório → reaparece no abrigo
  L.session.send(proto::Request{proto::ReqDev{proto::DevCommand::Hurt, 999, 0, 0}});
  bool died = false, respawned = false;
  for (int i = 0; i < 300 && !died; ++i) {
    L.session.send(cb.build(InputState{}, cc, false));
    L.step();
    for (const auto& ev : L.session.takeEvents())
      if (std::holds_alternative<proto::EvPlayerDied>(ev)) died = true;
  }
  REQUIRE(died);
  L.session.send(proto::Request{proto::ReqRespawn{}});
  for (int i = 0; i < 10 && !respawned; ++i) {
    L.step();
    for (const auto& ev : L.session.takeEvents())
      if (std::holds_alternative<proto::EvRespawned>(ev)) respawned = true;
  }
  CHECK(respawned);
  CHECK(L.world.self()->state == static_cast<std::uint8_t>(proto::PlayerState::Free));
  CHECK(L.world.self()->hp == Approx(L.world.self()->maxHp));
}

TEST_CASE("Localização cobre todas as mensagens e aplica os modificadores", "[client]") {
  const Localization& L = loc();
  for (std::size_t i = 0; i < static_cast<std::size_t>(proto::MessageId::Count); ++i)
    CHECK_FALSE(L.message(static_cast<proto::MessageId>(i)).empty());
  const ItemId ferro = gameData().items.find("minerio");
  CHECK(L.message(proto::MessageId::MarketSold, {proto::MsgArg::integer(3), proto::MsgArg::ref(proto::MsgArg::Type::Item, ferro.value),
                                                 proto::MsgArg::real(12.5)}) == "Vendeu 3× Minério de ferro por 12.5 moedas.");
  CHECK(L.message(proto::MessageId::EventNoneInBag, {proto::MsgArg::ref(proto::MsgArg::Type::Item, ferro.value)}) ==
        "Nenhum(a) minério de ferro na bolsa.");
  CHECK(L.message(proto::MessageId::ChannelCancelled, {proto::MsgArg::message(proto::MessageId::ChannelMounting),
                                                       proto::MsgArg::message(proto::MessageId::ChannelReasonCancelled)}) ==
        "Montando: cancelado.");
  CHECK(Localization::number(45) == "45");
  CHECK(Localization::number(0.1 + 0.2) == "0.30000000000000004");  // como o `${n}` do JS
  CHECK(L.message(compassMessage(0, -10)) == "ao norte");
  CHECK(L.message(compassMessage(10, 0)) == "a leste");
}

TEST_CASE("Alvos de uso: o mercado do Vale responde à tecla F de perto", "[client]") {
  Loop L;
  L.join();
  const GameplayLayout& lay = staticWorld().layout();
  std::size_t market = 0;
  for (std::size_t i = 0; i < lay.interactables.size(); ++i)
    if (lay.interactables[i].kind == "market" && lay.interactables[i].data == "vale") market = i;
  // o cliente monta a lista a partir do que conhece: coloca o jogador replicado no mercado
  std::vector<proto::EntityState> ents = L.world.entities();
  for (auto& e : ents)
    if (e.id == L.world.playerId()) {
      e.x = lay.interactables[market].x + 1;
      e.z = lay.interactables[market].z;
    }
  ClientWorld w;
  w.set(L.world.playerId(), ents, L.session.snapshots().latest());
  const auto t = nearestInRange(w, staticWorld());
  REQUIRE(t);
  CHECK(t->kind == proto::InteractKind::Market);
  CHECK(t->id == market);
  CHECK(loc().interactable(lay.interactables[market].label) == "Negociar no Mercado do Vale");
}

TEST_CASE("Câmera: começa atrás do personagem e o braço de mola não atravessa o relevo", "[client]") {
  const StaticMap& map = staticWorld().map(MapKind::Region);
  ThirdPersonCamera cam;
  CameraSubject p;
  p.pos = {0, map.groundHeight(0, 300), 300};
  InputState in;
  cam.update(1.0 / 60, true, p, in, false, map);
  const ViewCamera& v = cam.view();
  // yaw 0: a câmera fica ao sul (+z) e acima, olhando para o personagem
  CHECK(v.position.z > p.pos.z + 5);
  CHECK(v.position.y > p.pos.y + 5);
  CHECK(v.fovDeg == Approx(55));
  CHECK(v.position.y >= map.groundHeight(v.position.x, v.position.z) + 0.55 - 1e-9);
  // mirando: aproxima e fecha o campo de visão
  p.aiming = true;
  for (int i = 0; i < 60; ++i) cam.update(1.0 / 60, false, p, in, false, map);
  CHECK(cam.aimT() > 0.99);
  CHECK(cam.view().fovDeg < 40);
}

TEST_CASE("Mira: o raio pelo centro da tela acerta o inimigo à frente", "[client]") {
  const StaticMap& map = staticWorld().map(MapKind::Region);
  const double gx = 0, gz = 300, gy = map.groundHeight(gx, gz);
  proto::EntityState me;
  me.id = 1;
  me.kind = proto::EntityKind::Player;
  me.x = gx;
  me.y = gy;
  me.z = gz;
  proto::EntityState foe;
  foe.id = 2;
  foe.kind = proto::EntityKind::Enemy;
  foe.type = gameData().enemies.find("saqueador").value;
  foe.flags = proto::kFlagAlive | proto::kFlagHuman;
  foe.x = gx;
  foe.z = gz - 10;
  foe.y = map.groundHeight(foe.x, foe.z);
  proto::Snapshot snap;
  snap.self = proto::PrivateState{};
  ClientWorld w;
  w.set(1, {me, foe}, &snap);
  Ray r;
  r.origin = {gx, gy + 1.8, gz + 3};
  r.dir = glm::normalize(glm::dvec3(foe.x, foe.y + 2.3 * 0.45, foe.z) - r.origin);
  const AimResult a = computeAim(r, {gx, gy, gz}, 0, w, gameData(), map);
  CHECK(a.enemy == 2);
  CHECK(std::abs(a.yaw - 3.14159) < 0.05);  // para −z
}

TEST_CASE("Relevo desenhado coincide com o chão da simulação nos vértices", "[client]") {
  const StaticMap& map = staticWorld().map(MapKind::Turbulent);
  const TerrainBuild t = buildTerrain(map);
  REQUIRE(t.chunks.size() == 16);
  for (const TerrainChunk& c : t.chunks)
    for (std::size_t i = 0; i < c.lod[0].vertices.size(); i += 97) {
      const Vertex& v = c.lod[0].vertices[i];
      if (v.ny < 0) continue;
      const double h = map.terrain().terrainHeight(static_cast<double>(v.px), static_cast<double>(v.pz));
      const auto y = static_cast<double>(v.py);
      // saias descem um passo abaixo; os vértices de superfície batem com o relevo
      if (std::abs(y - h) < 0.9) CHECK(y == Approx(h).margin(1e-3));
    }
  const InstanceLists props = buildStaticProps(map, staticWorld().layout());
  CHECK(props.total() > 0);
}

TEST_CASE("Luz do dia: meio-dia claro, meia-noite escura, Turbulenta violeta", "[client]") {
  const SceneLighting noon = lightingAt(12, false), night = lightingAt(0, false), turb = lightingAt(12, true);
  CHECK(noon.daylight.day > 0.99);
  CHECK(noon.sunDir.y > 0.9f);
  CHECK(night.daylight.night > 0.99);
  CHECK(night.lightDir.y > 0);  // luar vem de cima
  CHECK(noon.skyTop.b > night.skyTop.b);
  CHECK(turb.fogDensity == Approx(0.012f));
  CHECK(turb.sunAmount == 0.0f);
}

TEST_CASE("PNG: assinatura, dimensões e CRC válidos", "[client]") {
  std::vector<std::uint8_t> px(4 * 3 * 2, 128);
  const auto png = encodePng(px, 3, 2);
  REQUIRE(png.size() > 33);
  CHECK(png[0] == 0x89);
  CHECK(png[1] == 'P');
  CHECK(png[16] == 0);
  CHECK(png[19] == 3);  // largura
  CHECK(png[23] == 2);  // altura
}
