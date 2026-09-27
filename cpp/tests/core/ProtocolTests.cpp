// Protocolo: ida e volta de cada mensagem, e leitura segura de bytes inválidos.
#include <catch2/catch_test_macros.hpp>

#include "core/protocol/ByteStream.h"
#include "core/protocol/GameEvents.h"
#include "core/protocol/PlayerCommand.h"
#include "core/protocol/Requests.h"
#include "core/protocol/Snapshot.h"

using namespace rpg::protocol;

TEST_CASE("PlayerCommand: ida e volta (ângulos e mira em float)", "[core][protocol]") {
  PlayerCommand c;
  c.seq = 12345;
  c.clientTick = 99;
  c.moveForward = 1;
  c.moveRight = -1;
  c.camYaw = 1.25;
  c.held = kHeldRun | kHeldAim;
  c.press(Press::Jump);
  c.press(Press::Jump);
  c.press(Press::SkillE);
  c.aimYaw = -0.5;
  c.aimPitch = 0.25;
  c.aimDist = 12.5;
  c.aimX = 10;
  c.aimY = 2;
  c.aimZ = -30.5;
  c.aimEnemy = 42;
  const auto back = decode<PlayerCommand>(encode(c));
  REQUIRE(back);
  CHECK(back->seq == 12345);
  CHECK(back->moveForward == 1);
  CHECK(back->moveRight == -1);
  CHECK(back->camYaw == 1.25);  // representável em float
  CHECK(back->has(kHeldRun));
  CHECK_FALSE(back->has(kHeldCrouch));
  CHECK(back->count(Press::Jump) == 2);
  CHECK(back->count(Press::SkillE) == 1);
  CHECK(back->aimEnemy == 42);
  CHECK(back->aimZ == -30.5);
}

TEST_CASE("Request: variante com o tipo certo", "[core][protocol]") {
  const Request r = ReqSell{2, 7, 3, true};
  const auto back = decode<Request>(encode(r));
  REQUIRE(back);
  REQUIRE(std::holds_alternative<ReqSell>(*back));
  const auto& s = std::get<ReqSell>(*back);
  CHECK(s.market == 2);
  CHECK(s.item == 7);
  CHECK(s.qty == 3);
  CHECK(s.all);

  const Request note = ReqBookNote{"Anotação com acentuação: ação, pão, órgão."};
  const auto n = decode<Request>(encode(note));
  REQUIRE(n);
  CHECK(std::get<ReqBookNote>(*n).text == "Anotação com acentuação: ação, pão, órgão.");
}

TEST_CASE("GameEvent: toast com argumentos, relatório de derrota e Livro", "[core][protocol]") {
  EvToast t{MessageId::MarketSold, {MsgArg::integer(3), MsgArg::ref(MsgArg::Type::Item, 5), MsgArg::real(12.5)}, ToastKind::Coin, 0};
  const auto back = decode<GameEvent>(encode(GameEvent{t}));
  REQUIRE(back);
  const auto& bt = std::get<EvToast>(*back);
  CHECK(bt.msg == MessageId::MarketSold);
  REQUIRE(bt.args.size() == 3);
  CHECK(bt.args[1] == MsgArg::ref(MsgArg::Type::Item, 5));

  DeathReport rep;
  rep.zone = 1;
  rep.place = 11;
  rep.cause = MsgArg::ref(MsgArg::Type::Enemy, 3);
  rep.lost = {{4, 3}, {1, 2}};
  rep.kept = {MsgArg::message(MessageId::KeptBook)};
  const auto d = decode<GameEvent>(encode(GameEvent{EvPlayerDied{rep}}));
  REQUIRE(d);
  CHECK(std::get<EvPlayerDied>(*d).report.lost.size() == 2);
  CHECK(std::get<EvPlayerDied>(*d).report.cause == rep.cause);

  BookEntry e;
  e.id = 7;
  e.key = "oficio";
  e.tmpl = "tally.oficio";
  e.count = 4;
  e.data = {{"ferramentas", 3}, {"lingote", 1}};
  e.updatedTime = 99.5;
  const auto b = decode<GameEvent>(encode(GameEvent{EvBookEntry{e, false}}));
  REQUIRE(b);
  const auto& be = std::get<EvBookEntry>(*b).entry;
  CHECK(be.key == "oficio");
  CHECK(be.data.size() == 2);
  CHECK(be.updatedTime == 99.5);
}

TEST_CASE("Snapshot: ida e volta com entidades e estado privado", "[core][protocol]") {
  Snapshot s;
  s.world.tick = 900;
  s.world.time = 30.0;
  s.world.vanished = {{1734, -1, 50.5}};
  s.world.markets.resize(2);
  s.world.markets[1].demand[7] = 0.835;
  PrivateState me;
  me.playerId = 5;
  me.hp = 87.5;
  me.inv = {{3, 4, 0, 100}, {9, 1, 17, 64.5}};
  me.weapon = ItemEntry{9, 1, 1, 82};
  me.stats["kills"] = 3;
  me.flags = {"disc_bosque"};
  s.self = me;
  EntityState e;
  e.id = 77;
  e.kind = EntityKind::Enemy;
  e.x = 100.25;
  e.flags = kFlagAlive | kFlagBar;
  e.name = "";
  s.entities = {e};
  const auto back = decode<Snapshot>(encode(s));
  REQUIRE(back);
  CHECK(back->world.tick == 900);
  CHECK(back->world.markets[1].demand.at(7) == 0.835);
  REQUIRE(back->self);
  CHECK(back->self->hp == 87.5);
  CHECK(back->self->inv.size() == 2);
  CHECK(back->self->weapon->uid == 1);
  CHECK(back->self->flags.contains("disc_bosque"));
  REQUIRE(back->entities.size() == 1);
  CHECK(back->entities[0].x == 100.25);
  CHECK((back->entities[0].flags & kFlagBar) != 0);
}

TEST_CASE("Reader: bytes truncados, sobrando ou absurdos não viram mensagem", "[core][protocol]") {
  PlayerCommand c;
  c.seq = 1;
  Bytes b = encode(c);
  Bytes cut(b.begin(), b.end() - 3);
  CHECK_FALSE(decode<PlayerCommand>(cut));
  Bytes extra = b;
  extra.push_back(0);
  CHECK_FALSE(decode<PlayerCommand>(extra));
  // lista que diz ter um bilhão de elementos
  Bytes huge = {0xff, 0xff, 0xff, 0xff, 0x0f};
  CHECK_FALSE(decode<std::vector<std::uint32_t>>(huge));
  // variante com índice fora da lista
  Bytes badVariant = {200};
  CHECK_FALSE(decode<Request>(badVariant));
  // float NaN na mira
  PlayerCommand nanCmd;
  nanCmd.aimYaw = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(decode<PlayerCommand>(encode(nanCmd)));
}
