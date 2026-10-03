#include <cmath>
#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "server/ServerHost.h"
#include "server/TestClient.h"
#include "server/persist/SaveRecords.h"
#include "server/persist/SaveStore.h"
#include "sim/Sim.h"
#include "sim/World.h"

namespace persist = rpg::server::persist;
namespace proto = rpg::protocol;
using rpg::test::gameData;
using rpg::test::staticWorld;
using rpg::test::TempDir;
using rpg::test::TestClient;

namespace {

rpg::ItemId itemId(const char* k) { return gameData().items.find(k); }

persist::Json roundTripText(const persist::Json& j) { return persist::Json::parse(j.dump()); }

int amount(const rpg::sim::ItemList& list, const char* k) { return rpg::sim::count(list, itemId(k)); }

}  // namespace

TEST_CASE("Personagem salvo e carregado volta igual (save.js saveGame/applySave)", "[persist]") {
  rpg::sim::World w(gameData(), staticWorld());
  const auto id = w.addPlayer({"Ana", "humano", "artesao", "kachujin"});
  rpg::sim::Sim& S = w.state();
  rpg::sim::Player& p = *S.player(id);
  p.coins = 123.5;
  rpg::sim::addTo(S.data, S.uids, p.inv, itemId("minerio"), 4);
  rpg::sim::addTo(S.data, S.uids, p.saddle, itemId("madeira"), 2);
  rpg::sim::addTo(S.data, S.uids, p.storage, itemId("lingote"), 3);
  rpg::sim::addTo(S.data, S.uids, p.stableBags, itemId("pele"), 1);
  p.equip.armor = rpg::sim::makeGear(S.uids, itemId("gibao"), 70);
  p.trainings.push_back("alquimista");
  rpg::sim::stat(p, "crafted", 2);
  rpg::sim::stat(p, "crafted_lingote", 2);
  p.flags.insert("disc_passagem");
  p.observed.insert(5);
  p.evt.contrib["ferramentas"] = 8;
  p.evt.pts = 8;
  p.evt.delivered["ferramentas"] = 1;
  rpg::sim::bookTally(S, p, "oficio", rpg::sim::BookCat::Feitos, rpg::sim::BookDomain::Trabalho, "tally.oficio", 2,
                      rpg::sim::TallyData{{"lingote", 2}}, {proto::MsgArg::ref(proto::MsgArg::Type::Item, itemId("minerio").value)});
  rpg::sim::bookLog(S, p, rpg::sim::BookCat::Historia, rpg::sim::BookDomain::Risco, "log.defeat",
                    {proto::MsgArg::str("lobo"), proto::MsgArg::message(proto::MessageId::CauseTest), proto::MsgArg::real(2.5),
                     proto::MsgArg::ref(proto::MsgArg::Type::Place, static_cast<std::uint16_t>(rpg::PlaceId::Passagem))});
  p.book.grantTitle("ferreira");
  p.book.setShownTitle("ferreira");
  const rpg::XZ spot{30.5, 380.25};
  REQUIRE(persist::isValidPosition(staticWorld(), spot.x, spot.z));
  p.pos = {spot.x, 0, spot.z};
  p.yaw = 1.25;

  const persist::Json rec = roundTripText(persist::exportCharacter(S, p));
  CHECK(rec["kind"] == "character");
  CHECK(rec["P"]["inv"].size() == p.inv.size());
  persist::Migration m;
  const auto migrated = persist::migrateCharacter(staticWorld(), rec, &m);
  REQUIRE(migrated);
  CHECK_FALSE(m.repositioned);
  CHECK(m.notes.empty());

  rpg::sim::World w2(gameData(), staticWorld());
  int dropped = 0;
  const auto id2 = persist::loadCharacter(w2, *migrated, 7, &dropped);
  const rpg::sim::Player& q = *w2.state().player(id2);
  CHECK(dropped == 0);
  CHECK(q.session == 7);
  CHECK(q.name == "Ana");
  CHECK(q.start == "artesao");
  CHECK(q.model == "kachujin");
  CHECK(q.coins == 123.5);
  CHECK(q.trainings == p.trainings);
  CHECK(amount(q.inv, "minerio") == amount(p.inv, "minerio"));
  CHECK(amount(q.inv, "pocao") == amount(p.inv, "pocao"));
  CHECK(amount(q.saddle, "madeira") == 2);
  CHECK(amount(q.storage, "lingote") == 3);
  CHECK(amount(q.stableBags, "pele") == 1);
  REQUIRE(q.equip.armor);
  CHECK(q.equip.armor->id == itemId("gibao"));
  CHECK(q.equip.armor->cond == 70);
  CHECK(q.equip.armor->uid != 0);
  REQUIRE(q.equip.weapon);
  CHECK(q.equip.weapon->cond == p.equip.weapon->cond);
  CHECK(q.stats == p.stats);
  CHECK(q.flags == p.flags);
  CHECK(q.observed == p.observed);
  CHECK(q.evt.get("ferramentas") == 8);
  CHECK(q.evt.pts == 8);
  CHECK(q.evt.delivered.at("ferramentas") == 1);
  CHECK(q.maxHp == 100 + gameData().items[itemId("gibao")].armorHp);
  CHECK(q.hp == q.maxHp);
  CHECK(q.pos.x == Catch::Approx(spot.x));
  CHECK(q.pos.z == Catch::Approx(spot.z));
  CHECK(q.yaw == Catch::Approx(1.25));
  // Livro: as mesmas entradas, na mesma ordem, com modelo e argumentos (a chegada não se repete)
  REQUIRE(q.book.entries().size() == p.book.entries().size());
  for (std::size_t i = 0; i < p.book.entries().size(); ++i) {
    const auto& a = p.book.entries()[i];
    const auto& b = q.book.entries()[i];
    CHECK(a.id == b.id);
    CHECK(a.key == b.key);
    CHECK(a.tmpl == b.tmpl);
    CHECK(a.args == b.args);
    CHECK(a.cat == b.cat);
    CHECK(a.domain == b.domain);
    CHECK(a.count == b.count);
    CHECK(a.data == b.data);
    CHECK(a.day == b.day);
    CHECK_FALSE(b.literal);
  }
  CHECK(q.book.seq() == p.book.seq());
  REQUIRE(q.book.titles().size() == 1);
  CHECK(q.book.titles()[0].id == "ferreira");
  CHECK(q.book.shownTitle() == std::optional<std::string>("ferreira"));
}

TEST_CASE("Mundo salvo: hora, evento, acampamento, céu e mercados voltam; evento concluído reabre a Passagem", "[persist]") {
  rpg::sim::World w(gameData(), staticWorld());
  rpg::sim::Sim& S = w.state();
  const auto vale = gameData().markets.find("vale").value;
  S.time = 5000;
  S.evt.progress = 40;
  S.evt.othersPts = 12;
  S.camp.pop = 4;
  S.camp.state = proto::CampPhase::Pressionado;
  S.skyState.vanished.push_back({12, 3, 4000});
  S.skyState.instrumentGiven = true;
  S.demand[vale][itemId("minerio")] = 0.6;

  const persist::Json rec = roundTripText(persist::exportWorld(S));
  rpg::sim::World w2(gameData(), staticWorld());
  persist::loadWorld(w2, rec);
  const rpg::sim::Sim& T = w2.state();
  CHECK(T.time == 5000);
  CHECK(T.evt.progress == 40);
  CHECK(T.evt.othersPts == 12);
  CHECK_FALSE(T.evt.done);
  CHECK(T.camp.pop == 4);
  CHECK(T.camp.state == proto::CampPhase::Pressionado);
  CHECK(T.camp.pending);
  REQUIRE(T.skyState.vanished.size() == 1);
  CHECK(T.skyState.vanished[0].index == 12);
  CHECK(T.skyState.vanished[0].constellation == 3);
  CHECK(T.skyState.instrumentGiven);
  CHECK(T.skyState.next == 5060);
  CHECK(T.demand[vale].at(itemId("minerio")) == 0.6);

  // concluído: guardas nos postos e ferramentas mais baratas (event.js applyReopenEffects)
  S.evt.done = true;
  S.evt.doneAt = 4900;
  rpg::sim::World w3(gameData(), staticWorld());
  const auto guards = [](const rpg::sim::Sim& s) {
    int n = 0;
    for (const auto& e : s.enemies) n += e->guardPost ? 1 : 0;
    return n;
  };
  const int before = guards(w3.state());  // o guarda do canteiro já existe
  persist::loadWorld(w3, roundTripText(persist::exportWorld(S)));
  CHECK(w3.state().evt.done);
  CHECK(w3.state().evt.doneAt == std::optional<double>(4900));
  CHECK(guards(w3.state()) == before + static_cast<int>(staticWorld().layout().guardPosts.size()));
  const auto alto = gameData().markets.find("alto").value;
  CHECK(w3.state().sell[alto][itemId("ferramentas").value] == std::optional<double>(19));
}

TEST_CASE("Migração do save: layout antigo, sem posição ou posição inválida voltam ao abrigo", "[persist]") {
  const auto& W = staticWorld();
  const rpg::XZ shelter = persist::safeSpawn(W);
  const persist::Json base = {{"v", 2}, {"profile", {{"name", "Rui"}}}, {"P", persist::Json::object()}};
  const auto posOf = [](const persist::Json& j) { return rpg::XZ{j["world"]["pos"][0].get<double>(), j["world"]["pos"][1].get<double>()}; };

  SECTION("layout do recorte de 400 × 400") {
    persist::Json d = base;
    d["world"] = {{"layout", 1}, {"pos", {10, 10}}, {"yaw", 0.5}};
    persist::Migration m;
    const auto r = persist::migrateCharacter(W, d, &m);
    REQUIRE(r);
    CHECK(m.layoutChanged);
    CHECK(m.repositioned);
    CHECK(m.fromLayout == 1);
    CHECK(m.toLayout == W.worldLayoutVersion());
    CHECK(posOf(*r).x == shelter.x);
    CHECK(posOf(*r).z == shelter.z);
    CHECK((*r)["world"]["layout"] == W.worldLayoutVersion());
    CHECK((*r)["v"] == persist::kCharacterVersion);
  }
  SECTION("save da versão 1, sem `world`") {
    persist::Migration m;
    const auto r = persist::migrateCharacter(W, base, &m);
    REQUIRE(r);
    CHECK(m.noPosition);
    CHECK(m.repositioned);
    CHECK(posOf(*r).x == shelter.x);
  }
  SECTION("posição fora do mundo, na Turbulenta ou que não é número") {
    for (const rpg::XZ bad : {rpg::XZ{0, 600}, rpg::XZ{1400, 0}, rpg::XZ{std::nan(""), 0}}) {
      persist::Json d = base;
      d["world"] = {{"layout", W.worldLayoutVersion()}, {"pos", {bad.x, bad.z}}, {"yaw", 0}};
      persist::Migration m;
      const auto r = persist::migrateCharacter(W, d, &m);
      REQUIRE(r);
      CHECK(m.repositioned);
      CHECK(posOf(*r).x == shelter.x);
    }
  }
  SECTION("posição válida fica como está") {
    persist::Json d = base;
    d["world"] = {{"layout", W.worldLayoutVersion()}, {"pos", {30.5, 380.25}}, {"yaw", 0}};
    persist::Migration m;
    const auto r = persist::migrateCharacter(W, d, &m);
    REQUIRE(r);
    CHECK_FALSE(m.repositioned);
    CHECK(posOf(*r).x == 30.5);
  }
  SECTION("o que não é um personagem é recusado") {
    CHECK_FALSE(persist::migrateCharacter(W, persist::Json::array()));
    CHECK_FALSE(persist::migrateCharacter(W, persist::Json{{"P", 1}}));
  }
}

TEST_CASE("Não se salva dentro da Turbulenta", "[persist]") {
  rpg::sim::Player p;
  CHECK(persist::canSave(p));
  p.turb.inside = true;
  CHECK_FALSE(persist::canSave(p));
  p.turb.inside = false;
  p.map = rpg::MapKind::Turbulent;
  CHECK_FALSE(persist::canSave(p));
}

TEST_CASE("Arquivos de save: nome do arquivo, gravação atômica, lista e remoção", "[persist]") {
  CHECK(persist::SaveStore::fileNameFor("Ana") == "ana.json");
  CHECK(persist::SaveStore::fileNameFor("ana") == persist::SaveStore::fileNameFor("ANA"));
  CHECK(persist::SaveStore::fileNameFor("Élis da Silva") == "%C3%89lis%20da%20silva.json");
  CHECK(persist::SaveStore::fileNameFor("../x") == "%2E%2E%2Fx.json");
  CHECK(persist::SaveStore::fileNameFor("") == "_.json");

  TempDir dir("store");
  const persist::SaveStore store(dir.path / "saves");
  CHECK_FALSE(store.loadWorld());
  CHECK_FALSE(store.hasCharacter("Ana"));
  CHECK(store.characters().empty());
  const persist::Json ana = {{"profile", {{"name", "Ana"}}}, {"P", {{"coins", 3}}}};
  const persist::Json elis = {{"profile", {{"name", "Élis"}}}, {"P", {{"coins", 9}}}};
  REQUIRE(store.saveCharacter("Ana", ana));
  REQUIRE(store.saveCharacter("Élis", elis));
  REQUIRE(store.saveWorld({{"time", 12}}));
  CHECK(store.hasCharacter("ana"));
  CHECK(store.loadCharacter("ANA") == ana);
  CHECK((*store.loadWorld())["time"] == 12);
  CHECK(store.characters() == std::vector<std::string>{"Élis", "Ana"});  // ordem dos arquivos: %C3… antes de a
  CHECK_FALSE(std::filesystem::exists(dir.path / "saves" / "characters" / "ana.json.tmp"));

  // arquivo corrompido: ignorado, sem derrubar o servidor
  { std::ofstream(dir.path / "saves" / "characters" / "ana.json") << "{ quebrado"; }
  CHECK_FALSE(store.loadCharacter("Ana"));
  CHECK(store.removeCharacter("Ana"));
  CHECK_FALSE(store.hasCharacter("Ana"));
}

TEST_CASE("Save da demo importado vira personagem e mundo deste projeto", "[persist]") {
  const auto demo = persist::readJsonFile(rpg::pathFromUtf8(RPG_TEST_SERVER_FIXTURES "/demo-save-v2.json"));
  REQUIRE(demo);
  const auto imported = persist::importDemoSave(*demo);
  REQUIRE(imported);
  const auto profile = persist::profileOf(gameData(), imported->character);
  CHECK(profile.name == "Iara");
  CHECK(profile.start == "artesao");
  CHECK(profile.model == "eve");

  rpg::sim::World w(gameData(), staticWorld());
  persist::loadWorld(w, roundTripText(imported->world));
  persist::Migration m;
  const auto rec = persist::migrateCharacter(staticWorld(), roundTripText(imported->character), &m);
  REQUIRE(rec);
  CHECK_FALSE(m.repositioned);
  int dropped = 0;
  const auto id = persist::loadCharacter(w, *rec, 0, &dropped);
  const rpg::sim::Sim& S = w.state();
  const rpg::sim::Player& p = *S.player(id);

  CHECK(dropped == 1);  // "item_que_nao_existe"
  CHECK(p.coins == 87.5);
  CHECK(p.trainings == std::vector<std::string>{"artesao", "alquimista"});
  CHECK(amount(p.inv, "pocao") == 3);
  CHECK(amount(p.inv, "minerio") == 4);
  CHECK(amount(p.inv, "espada") == 1);
  CHECK(amount(p.saddle, "madeira") == 5);
  CHECK(amount(p.storage, "gibao") == 1);
  REQUIRE(p.equip.weapon);
  CHECK(p.equip.weapon->id == itemId("martelo"));
  CHECK(p.equip.weapon->cond == 74);
  REQUIRE(p.equip.armor);
  CHECK(p.equip.armor->cond == 55);
  CHECK_FALSE(p.equip.bag);
  CHECK(p.flags == std::set<std::string>{"disc_mina", "disc_passagem"});
  CHECK(rpg::sim::statOf(p, "crafted_lingote") == 2);
  CHECK(p.stats.front().first == "kills");  // ordem de inserção do G.stats
  CHECK(p.observed == std::set<int>{41});
  CHECK(p.evt.get("ferramentas") == 8);
  CHECK(p.evt.pts == 13.5);
  CHECK(p.pos.x == Catch::Approx(30.5));
  CHECK(p.yaw == Catch::Approx(1.571));
  // Livro em texto pronto
  REQUIRE(p.book.entries().size() == 3);
  const auto& oficio = p.book.entries()[1];
  CHECK(oficio.literal);
  CHECK(oficio.literalTitle == "Ofício na forja");
  CHECK(oficio.count == 3);
  CHECK(oficio.isPublic);
  CHECK(oficio.domain == rpg::sim::BookDomain::Trabalho);
  CHECK(oficio.data.size() == 2);
  CHECK(p.book.entries()[2].personal);
  CHECK(p.book.seq() == 4);
  REQUIRE(p.book.titles().size() == 1);
  CHECK(p.book.titles()[0].literalName == "Ferreira do Vale");
  CHECK(p.book.shownTitle() == std::optional<std::string>("ferreira"));
  // mundo
  CHECK(S.time == Catch::Approx(1834.6));
  CHECK(S.evt.progress == 34.5);
  CHECK(S.evt.playersPts == 13.5);
  CHECK(S.camp.pop == 6);
  CHECK(S.camp.state == proto::CampPhase::Pressionado);
  REQUIRE(S.skyState.vanished.size() == 1);
  CHECK(S.skyState.vanished[0].index == 41);
  CHECK(S.demand[gameData().markets.find("vale").value].at(itemId("minerio")) == 0.78);
  CHECK(S.sell[gameData().markets.find("alto").value][itemId("ferramentas").value] == std::optional<double>(16));

  // o Livro importado viaja ao cliente como texto pronto
  const proto::EvBookSync sync = w.bookSync(id);
  REQUIRE(sync.entries.size() == 3);
  CHECK(sync.entries[0].literalText.starts_with("Iara chegou"));
}

TEST_CASE("ServerHost: sair salva, Continuar retoma, Nova trajetória apaga; o mundo sobrevive ao reinício", "[persist][server]") {
  TempDir dir("host");
  rpg::server::ServerConfig cfg;
  cfg.saveDir = dir.path;
  cfg.devCommands = true;
  cfg.autosaveEvery = 1;
  double savedTime = 0;
  {
    rpg::net::LocalHub hub;
    rpg::server::ServerHost host(gameData(), staticWorld(), cfg, hub.serverEndpoint());
    TestClient a(hub.connectClient());
    host.runTicks(1);
    a.hello("Ana");
    host.runTicks(2);
    a.drain();
    REQUIRE(a.welcome);
    CHECK_FALSE(a.welcome->resumed);
    CHECK(host.store()->hasCharacter("Ana"));  // personagem novo já tem save

    a.dev(proto::DevCommand::Coins, 77);
    host.runTicks(2);
    a.request(proto::ReqLeave{false});
    host.runTicks(2);
    CHECK(host.world().state().players.empty());
    CHECK((*host.store()->loadCharacter("Ana"))["P"]["coins"] == 117);

    // o mesmo nome em outra sessão é recusado enquanto o personagem está no mundo
    a.hello("ana", "espadachim", true);
    host.runTicks(2);
    a.drain();
    REQUIRE(a.welcome);
    CHECK(a.welcome->resumed);
    CHECK(a.welcome->migration == 0);
    CHECK(host.world().state().player(a.welcome->playerId)->coins == 117);
    TestClient b(hub.connectClient());
    host.runTicks(1);
    b.hello("ANA");
    host.runTicks(2);
    b.drain();
    CHECK(b.rejected);

    host.runTicks(40);  // autosave a cada 1 s
    CHECK(std::filesystem::exists(dir.path / "world.json"));
    savedTime = (*host.store()->loadWorld())["time"].get<double>();
    CHECK(savedTime > 0.5);

    // pausa → Nova trajetória: o save some
    a.request(proto::ReqLeave{true});
    host.runTicks(2);
    CHECK_FALSE(host.store()->hasCharacter("Ana"));
    a.hello("Ana", "espadachim", true);  // sem save: começa de novo
    host.runTicks(2);
    a.drain();
    REQUIRE(a.welcome);
    CHECK_FALSE(a.welcome->resumed);
    CHECK(host.world().state().player(a.welcome->playerId)->coins == 40);
    host.saveAll();
  }
  // reinício do servidor: o mundo volta do arquivo e o personagem pode ser retomado
  rpg::net::LocalHub hub;
  rpg::server::ServerHost host(gameData(), staticWorld(), cfg, hub.serverEndpoint());
  CHECK(host.world().time() >= savedTime);
  TestClient a(hub.connectClient());
  host.runTicks(1);
  a.hello("Ana", "espadachim", true);
  host.runTicks(2);
  a.drain();
  REQUIRE(a.welcome);
  CHECK(a.welcome->resumed);
}
