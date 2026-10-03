// Paridade da simulação com a demo: reproduz os roteiros de demo/tools/bake/scenarios.mjs e compara,
// quadro a quadro, com o rastro que as funções de jogo da própria demo produziram no bake
// (fixtures/sim-parity.json). A comparação é exata: o hash FNV-1a dos bits de todos os números do
// quadro (jogador, inimigos, projéteis). Na primeira divergência, o teste mostra o quadro completo
// gravado mais próximo, lado a lado, para achar o campo.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "TestData.h"
#include "core/JsMath.h"
#include "core/Math.h"
#include "core/Paths.h"
#include "core/Random.h"
#include "core/Rng.h"
#include "sim/World.h"

using nlohmann::json;
namespace sim = rpg::sim;
using rpg::XZ;

namespace {

const json& fixture() {
  static const json j = [] {
    std::ifstream in(rpg::pathFromUtf8(RPG_TEST_FIXTURES_DIR) / "sim-parity.json");
    std::stringstream ss;
    ss << in.rdbuf();
    return json::parse(ss.str());
  }();
  return j;
}

std::string fnv(const std::vector<double>& values) {
  std::uint32_t h = 0x811c9dc5u;
  for (const double v : values) {
    unsigned char b[8];
    std::memcpy(b, &v, 8);
    for (const unsigned char c : b) {
      h ^= c;
      h *= 0x01000193u;
    }
  }
  char buf[9];
  std::snprintf(buf, sizeof buf, "%08x", h);
  return buf;
}

sim::GearSlot slotOf(const std::string& s) {
  if (s == "weapon") return sim::GearSlot::Weapon;
  if (s == "armor") return sim::GearSlot::Armor;
  if (s == "bag") return sim::GearSlot::Bag;
  return sim::GearSlot::Mount;
}

std::optional<sim::Gear>& equipSlot(sim::Player& p, const std::string& s) {
  if (s == "weapon") return p.equip.weapon;
  if (s == "armor") return p.equip.armor;
  if (s == "bag") return p.equip.bag;
  return p.equip.mount;
}

class Runner {
 public:
  explicit Runner(const json& spec) : spec_(spec) {
    const auto& data = rpg::test::gameData();
    const auto& statics = rpg::test::staticWorld();
    const bool boot = spec.value("boot", false);
    sim::SimOptions o;
    if (!boot) {
      o.populate = false;
      o.travelers = false;
      const auto on = [&](const char* k) {
        if (!spec.contains("systems")) return true;
        for (const auto& x : spec["systems"])
          if (x == k) return true;
        return false;
      };
      o.stepMount = on("mount");
      o.stepEnemies = on("enemies");
      o.stepGroups = on("groups");
      o.stepProjectiles = on("projectiles");
      o.stepCamp = on("camp");
      o.stepEvent = on("event");
      o.stepStars = on("stars");
      o.stepTurbulent = on("turbulent");
      o.stepMarkets = on("markets");
      o.stepLootBags = on("lootbags");
      o.stepNpcs = on("npcs");
      o.stepInteract = on("interact");
      o.stepDiscovery = on("discovery");
    }
    const std::int32_t seed = boot ? fixture()["bootSeed"].get<std::int32_t>() : spec["seed"].get<std::int32_t>();
    world_ = std::make_unique<sim::World>(data, statics, std::make_unique<rpg::Mulberry32Random>(seed), o);
    sim::Sim& s = world_->state();
    if (!boot) {
      s.time = spec.value("time", 0.0);
      s.camp.pending = false;
    }
    const json& pr = spec["profile"];
    sim::PlayerProfile profile{pr["name"], pr["origin"], pr["start"], pr.value("model", "paladina")};
    player_ = world_->addPlayer(profile, 0, boot);
    if (!boot && spec.value("camp", false)) sim::initCamp(s);
    if (spec.contains("player")) applyPlayer(spec["player"]);
    if (spec.contains("enemies"))
      for (const auto& e : spec["enemies"]) spawn(e);
  }

  sim::Sim& s() { return world_->state(); }
  sim::Player& P() { return *s().player(player_); }

  void applyPlayer(const json& o) {
    sim::Player& p = P();
    if (o.contains("x")) teleport(o["x"], o["z"]);
    if (o.contains("yaw")) p.yaw = o["yaw"];
    if (o.contains("coins")) p.coins = o["coins"];
    if (o.contains("hp")) p.hp = o["hp"];
    if (o.contains("vigor")) p.vigor = o["vigor"];
    if (o.contains("trainings"))
      for (const auto& t : o["trainings"])
        if (!sim::hasTraining(p, t.get<std::string>())) p.trainings.push_back(t);
    if (o.value("clearInv", false)) p.inv.clear();
    if (o.contains("inv")) {
      for (const auto& e : o["inv"]) {
        std::optional<double> cond;
        if (e.size() > 2 && !e[2].is_null()) cond = e[2].get<double>();
        rpg::sim::addTo(s().data, s().uids, p.inv, sim::item(s(), e[0].get<std::string>()), e[1].get<int>(), cond);
      }
    }
    if (o.contains("storage"))
      for (const auto& e : o["storage"])
        rpg::sim::addTo(s().data, s().uids, p.storage, sim::item(s(), e[0].get<std::string>()), e[1].get<int>());
    if (o.contains("equip"))
      for (const auto& e : o["equip"]) equip(e[0], e.size() > 1 && !e[1].is_null() ? e[1].get<std::string>() : "", e.size() > 2 ? e[2].get<double>() : 100);
    if (o.value("mounted", false)) {
      sim::spawnMountNear(s(), p);
      sim::mountUp(s(), p);
    }
  }

  void teleport(double x, double z) {
    sim::Player& p = P();
    const rpg::MapKind map = s().statics.mapAtDemoX(x);
    if (map != p.map) p.map = map;
    p.pos = {x, s().mapOf(map).groundHeight(x, z), z};
  }

  void equip(const std::string& slot, const std::string& id, double cond) {
    auto& g = equipSlot(P(), slot);
    if (id.empty()) g.reset();
    else g = sim::makeGear(s().uids, sim::item(s(), id), cond);
  }

  void spawn(const json& e) {
    sim::SpawnOpts o;
    if (e.contains("leash")) o.leash = e["leash"].get<double>();
    if (e.contains("post")) {
      sim::Enemy::Post post{e["post"]["x"], e["post"]["z"], std::nullopt};
      if (e["post"].contains("yaw")) post.yaw = e["post"]["yaw"].get<double>();
      o.post = post;
    }
    if (e.contains("patrol"))
      for (const auto& pt : e["patrol"]) o.patrol.push_back({pt[0].get<double>(), pt[1].get<double>()});
    const double x = e["x"], z = e["z"];
    o.map = s().statics.mapAtDemoX(x);
    const sim::Enemy& en = sim::spawnEnemy(s(), sim::enemyType(s(), e["type"].get<std::string>()), x, z, o);
    spawned_.push_back(en.id);
    lastSeen_[en.id] = {en.pos, en.alive, en.faction, en.barY};
  }

  void call(const json& c) {
    const std::string op = c[0];
    sim::Sim& S = s();
    sim::Player& p = P();
    const auto itemAt = [&](int i) { return sim::item(S, c[static_cast<std::size_t>(i)].get<std::string>()); };
    const auto market = [&](int i) { return S.data.markets.find(c[static_cast<std::size_t>(i)].get<std::string>()); };
    if (op == "sell") sim::sell(S, p, market(1), itemAt(2), c[3]);
    else if (op == "buy") sim::buy(S, p, market(1), itemAt(2), c[3]);
    else if (op == "craft") sim::craft(S, p, rpg::RecipeId{c[1].get<std::uint16_t>()}, c[2].get<bool>());
    else if (op == "repair") sim::repair(S, p, slotOf(c[1]));
    else if (op == "learn") sim::learn(S, p, S.data.trainings.find(c[1].get<std::string>()));
    else if (op == "kit") sim::restartKit(S, p);
    else if (op == "buyMount") sim::buyMount(S, p);
    else if (op == "deliver") sim::deliver(S, p, itemAt(1));
    else if (op == "give") sim::receive(S, p, itemAt(1), c[2]);
    else if (op == "kill") {
      if (sim::Enemy* e = S.enemy(spawned_.at(c[1].get<std::size_t>()))) {
        sim::HurtOpts o;
        o.attacker = p.id;
        sim::hurtEnemy(S, *e, 9999, o);
      }
    } else if (op == "killCamp") {
      const std::size_t i = c[1];
      if (i < S.camp.members.size())
        if (sim::Enemy* e = S.enemy(S.camp.members[i]); e && e->alive) {
          sim::HurtOpts o;
          o.attacker = p.id;
          sim::hurtEnemy(S, *e, 9999, o);
        }
    } else if (op == "hurt") {
      sim::DamageSource src;
      src.x = p.pos.x + 1;
      src.z = p.pos.z;
      src.cause = rpg::protocol::MessageId::CauseTest;
      sim::hurtPlayer(S, p, c[1], src);
    } else if (op == "portal") sim::forcePortalNear(S, p);
    else if (op == "enter") sim::enterTurbulent(S, p);
    else if (op == "respawn") sim::respawn(S, p);
    else if (op == "observe") sim::observe(S, p, S.nightNow);
    else if (op == "endObserve") sim::endObserve(S, p);
    else if (op == "time") S.time += c[1].get<double>();
    else if (op == "spawn") spawn(c[1]);
    else if (op == "composeCamp") sim::initCamp(S);
    else if (op == "clickAttack") {
      sim::ClickCommand cmd;
      cmd.type = sim::ClickCommand::Type::Attack;
      cmd.enemy = spawned_.at(c[1].get<std::size_t>());
      p.cmd = cmd;
    } else if (op == "wield") {
      p.equip.weapon = sim::makeGear(S.uids, itemAt(1), c.size() > 2 ? c[2].get<double>() : 100);
      p.warnedBroken = false;
    } else if (op == "equip") {
      equip(c[1], c.size() > 2 && !c[2].is_null() ? c[2].get<std::string>() : "", c.size() > 3 ? c[3].get<double>() : 100);
    } else if (op == "coins") p.coins += c[1].get<double>();
    else if (op == "tp") teleport(c[1], c[2]);
    else if (op == "refresh") {
      p.cd.q = 0;
      p.cd.e = 0;
      p.vigor = p.maxVigor;
      p.hp = p.maxHp;
    } else FAIL("ação desconhecida no roteiro: " << op);
  }

  // harness `aimTarget` + `__aimHook`
  void aim(const json* spec, rpg::protocol::PlayerCommand& cmd) {
    sim::Player& p = P();
    double tx, ty, tz;
    std::uint32_t enemy = 0;
    if (spec && spec->contains("enemy")) {
      const sim::EntityId id = spawned_.at((*spec)["enemy"].get<std::size_t>());
      const Seen& e = lastSeen_.at(id);
      tx = e.pos.x;
      ty = e.pos.y + (e.barY != 0 ? e.barY : 2.4) * 0.45;
      tz = e.pos.z;
      if (e.alive && e.faction != rpg::Faction::Guard) enemy = id;
    } else if (spec && spec->contains("point")) {
      tx = (*spec)["point"][0];
      ty = (*spec)["point"][1];
      tz = (*spec)["point"][2];
    } else {
      const double d = spec && spec->contains("ahead") ? (*spec)["ahead"].get<double>() : 20;
      tx = p.pos.x + rpg::js::sin(p.yaw) * d;
      ty = p.pos.y + 1.45;
      tz = p.pos.z + rpg::js::cos(p.yaw) * d;
    }
    const double dx = tx - p.pos.x, dz = tz - p.pos.z, horiz = rpg::jsHypot(dx, dz);
    cmd.aimX = tx;
    cmd.aimY = ty;
    cmd.aimZ = tz;
    cmd.aimEnemy = enemy;
    cmd.aimYaw = rpg::js::atan2(dx, dz);
    const double dy = ty - (p.pos.y + 1.45);
    const double pitch = rpg::js::atan2(dy, std::max(1.2, horiz));
    cmd.aimPitch = pitch < -0.85 ? -0.85 : pitch > 0.85 ? 0.85 : pitch;
    cmd.aimDist = rpg::jsHypot3(dx, dy, dz);
  }

  std::vector<double> frameValues() {
    std::vector<double> v;
    sim::Player& p = P();
    v.insert(v.end(), {p.pos.x, p.pos.y, p.pos.z, p.yaw, static_cast<double>(p.state), p.stateT, p.hp, p.vigor, p.coins,
                       p.air ? p.air->y : -1.0, p.speedNow});
    v.push_back(static_cast<double>(s().enemies.size()));
    for (const auto& ep : s().enemies) {
      const sim::Enemy& e = *ep;
      v.insert(v.end(), {static_cast<double>(e.type.value), e.alive ? 1.0 : 0.0, e.pos.x, e.pos.y, e.pos.z, e.yaw, e.hp,
                         static_cast<double>(e.state), e.t});
    }
    v.push_back(static_cast<double>(s().projectiles.size()));
    for (const auto& pp : s().projectiles) v.insert(v.end(), {pp->x, pp->y, pp->z});
    return v;
  }

  // Roda o roteiro; devolve o índice do primeiro quadro divergente (-1 se nenhum).
  int run(std::string& report) {
    using rpg::protocol::Press;
    const json& frames = spec_["frames"];
    const json& trace = spec_["trace"]["frames"];
    const double dt = spec_.value("dt", 1.0 / 30.0);
    rpg::protocol::PlayerCommand cmd;
    int first = -1;
    for (std::size_t f = 0; f < frames.size(); ++f) {
      const json& fr = frames[f];
      if (fr.contains("calls"))
        for (const auto& c : fr["calls"]) call(c);
      std::set<std::string> keys;
      if (fr.contains("keys"))
        for (const auto& k : fr["keys"]) keys.insert(k);
      std::set<std::string> press;
      if (fr.contains("press"))
        for (const auto& k : fr["press"]) press.insert(k);
      cmd.seq++;
      cmd.moveForward = static_cast<std::int8_t>((keys.contains("KeyW") ? 1 : 0) - (keys.contains("KeyS") ? 1 : 0));
      cmd.moveRight = static_cast<std::int8_t>((keys.contains("KeyD") ? 1 : 0) - (keys.contains("KeyA") ? 1 : 0));
      cmd.camYaw = fr.value("cam", 0.0);
      cmd.held = 0;
      if (keys.contains("ShiftLeft")) cmd.held |= rpg::protocol::kHeldRun;
      if (keys.contains("ControlLeft")) cmd.held |= rpg::protocol::kHeldCrouch;
      if (fr.value("right", false)) cmd.held |= rpg::protocol::kHeldAim;
      if (fr.value("ui", false)) cmd.held |= rpg::protocol::kHeldUiOpen;
      if (press.contains("Space")) cmd.press(Press::Jump);
      if (press.contains("KeyC")) cmd.press(Press::Dodge);
      if (press.contains("KeyQ")) cmd.press(Press::SkillQ);
      if (press.contains("KeyE")) cmd.press(Press::SkillE);
      if (press.contains("Digit1") && !keys.contains("Tab")) cmd.press(Press::Potion);
      if (fr.value("fire", false)) cmd.press(Press::Fire);
      if (press.contains("KeyF")) cmd.press(Press::Interact);
      if (press.contains("KeyR")) cmd.press(Press::Mount);
      aim(fr.contains("aim") && !fr["aim"].is_null() ? &fr["aim"] : nullptr, cmd);
      world_->setCommand(player_, cmd);
      world_->step(dt);
      world_->takeEvents();
      for (const auto& ep : s().enemies) lastSeen_[ep->id] = {ep->pos, ep->alive, ep->faction, ep->barY};
      for (auto& [id, seen] : lastSeen_)
        if (!s().enemy(id)) seen.alive = false;

      const std::vector<double> v = frameValues();
      const std::string h = fnv(v);
      if (h != trace[f]["h"].get<std::string>()) {
        // RPG_PARITY_CONTINUE=1: segue até o fim e descreve cada quadro gravado que diverge
        if (std::getenv("RPG_PARITY_CONTINUE")) {
          if (trace[f].contains("p")) report += describe(static_cast<int>(f), v);
          if (first < 0) first = static_cast<int>(f);
          continue;
        }
        report = describe(static_cast<int>(f), v);
        return static_cast<int>(f);
      }
    }
    return first;
  }

  // Estado final (harness `summary`): inventários, moedas, estatísticas, Livro e sistemas do mundo.
  void checkEnd() {
    const json& end = spec_["trace"]["end"];
    sim::Sim& S = s();
    sim::Player& p = P();
    const auto items = [&](const sim::ItemList& list) {
      json out = json::array();
      for (const auto& e : list) {
        json x = json::array({S.data.items.key(e.id), e.amount()});
        x.push_back(e.uid ? json(e.cond) : json(nullptr));
        out.push_back(x);
      }
      return out;
    };
    CHECK(items(p.inv) == end["inv"]);
    CHECK(items(p.saddle) == end["saddle"]);
    CHECK(items(p.storage) == end["storage"]);
    CHECK(items(p.stableBags) == end["stableBags"]);
    json equip = json::array();
    for (const auto* g : {&p.equip.weapon, &p.equip.armor, &p.equip.bag, &p.equip.mount})
      equip.push_back(*g ? json::array({S.data.items.key((*g)->id), (*g)->cond}) : json(nullptr));
    CHECK(equip == end["equip"]);
    CHECK(p.coins == end["coins"].get<double>());
    CHECK(json(p.trainings) == end["trainings"]);
    json stats = json::array();
    for (const auto& [k, v] : p.stats) stats.push_back(json::array({k, v}));
    CHECK(stats == end["stats"]);
    CHECK(json(std::vector<std::string>(p.flags.begin(), p.flags.end())) == end["flags"]);
    json book = json::array();
    for (const auto& e : p.book.entries()) book.push_back(json::array({e.key.rfind("turb", 0) == 0 ? "turb" : e.key, e.count}));
    json jsBook = end["book"];
    for (auto& e : jsBook)
      if (e[0].get<std::string>().rfind("turb", 0) == 0) e[0] = "turb";  // a chave da demo leva o G.time
    CHECK(book == jsBook);
    json titles = json::array();
    for (const auto& t : p.book.titles()) titles.push_back(t.id);
    CHECK(titles == end["titles"]);
    static constexpr const char* kCamp[] = {"estabelecido", "pressionado", "deslocado", "recuperacao"};
    CHECK(json::array({kCamp[static_cast<int>(S.camp.state)], S.camp.pop}) == end["camp"]);
    CHECK(S.evt.progress == end["evt"][0].get<double>());
    CHECK((S.evt.done ? 1 : 0) == end["evt"][1].get<int>());
    CHECK(p.evt.pts == end["evt"][2].get<double>());
    CHECK(S.evt.othersPts == end["evt"][3].get<double>());
    json sky = json::array();
    for (const auto& v : S.skyState.vanished) sky.push_back(json::array({v.index, v.time}));
    CHECK(sky == end["sky"]);
    json bags = json::array();
    for (const auto& bp : S.lootBags) {
      json its = json::array();
      for (const auto& e : bp->items) its.push_back(json::array({S.data.items.key(e.id), e.amount()}));
      bags.push_back(json::array({bp->x, bp->z, its}));
    }
    CHECK(bags == end["bags"]);
    if (S.turb.portal) CHECK(json::array({S.turb.portal->x, S.turb.portal->z, S.turb.portal->expires}) == end["portal"]);
    else CHECK(end["portal"].is_null());
    for (int mk = 0; mk < 2; ++mk) {
      const rpg::MarketId id = S.data.markets.find(mk == 0 ? "vale" : "alto");
      std::map<std::string, double> mine, theirs;
      for (const auto& [item, d] : S.demand[id.value]) mine[S.data.items.key(item)] = d;
      for (const auto& e : end["demand"][static_cast<std::size_t>(mk)]) theirs[e[0].get<std::string>()] = e[1].get<double>();
      CHECK(mine == theirs);
    }
    json nodes = json::array();
    for (const auto& n : S.nodes) nodes.push_back(json::array({n.charges, n.regrowAt}));
    CHECK(nodes == end["nodes"]);
    json npcs = json::array();
    for (const auto& n : S.npcs)
      if (n.role == sim::NpcRole::Traveler) npcs.push_back(json::array({n.pos.x, n.pos.z}));
    if (spec_.value("boot", false)) CHECK(npcs == end["npcs"]);
  }

  std::string describe(int f, const std::vector<double>& mine) {
    const json& trace = spec_["trace"]["frames"];
    std::ostringstream out;
    out.precision(17);
    out << "roteiro '" << spec_["name"].get<std::string>() << "': divergiu no quadro " << f << "\n";
    const json& t = trace[static_cast<std::size_t>(f)];
    if (t.contains("p")) {
      std::vector<double> js;
      for (const auto& x : t["p"]) js.push_back(x);
      js.push_back(static_cast<double>(t["e"].size()));
      for (const auto& e : t["e"])
        for (const auto& x : e) js.push_back(x);
      js.push_back(static_cast<double>(t["pr"].size()));
      for (const auto& e : t["pr"])
        for (const auto& x : e) js.push_back(x);
      for (std::size_t i = 0; i < std::max(js.size(), mine.size()); ++i) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double a = i < js.size() ? js[i] : nan, b = i < mine.size() ? mine[i] : nan;
        if (a != b) out << "  [" << i << "] demo " << a << "  c++ " << b << "\n";
      }
    } else {
      out << "  (quadro sem rastro completo; o gravado mais próximo antes dele passou)\n";
      out << "  jogador c++:";
      for (std::size_t i = 0; i < 11 && i < mine.size(); ++i) out << " " << mine[i];
      out << "\n";
    }
    return out.str();
  }

 private:
  struct Seen {
    sim::Vec3d pos;
    bool alive = true;
    rpg::Faction faction = rpg::Faction::Beast;
    double barY = 2.3;
  };
  const json& spec_;
  std::unique_ptr<sim::World> world_;
  sim::EntityId player_ = 0;
  std::vector<sim::EntityId> spawned_;
  std::map<sim::EntityId, Seen> lastSeen_;
};

void runScenario(const std::string& name) {
  for (const auto& sc : fixture()["scenarios"]) {
    if (sc["name"] != name) continue;
    Runner r(sc);
    std::string report;
    const int f = r.run(report);
    INFO(report);
    CHECK(f == -1);
    if (f == -1) r.checkEnd();
    return;
  }
  FAIL("roteiro não encontrado: " << name);
}

}  // namespace

TEST_CASE("Paridade da simulação: boot completo da demo", "[parity][sim]") { runScenario("boot"); }
TEST_CASE("Paridade da simulação: motor (andar, correr, agachar, mirar, pular, esquivar)", "[parity][sim]") { runScenario("motor"); }
TEST_CASE("Paridade da simulação: vau do rio e ponte", "[parity][sim]") { runScenario("rio"); }
TEST_CASE("Paridade da simulação: montaria", "[parity][sim]") { runScenario("montaria"); }
TEST_CASE("Paridade da simulação: estados de carga e armadura", "[parity][sim]") { runScenario("carga"); }
TEST_CASE("Paridade da simulação: espada contra grupo", "[parity][sim]") { runScenario("espada"); }
TEST_CASE("Paridade da simulação: arco mirado e técnicas", "[parity][sim]") { runScenario("arco"); }
TEST_CASE("Paridade da simulação: técnicas de magia (gelo, fogo, natureza, vento)", "[parity][sim]") { runScenario("magias-a"); }
TEST_CASE("Paridade da simulação: técnicas de magia (maldição, fera, vital, sagrado)", "[parity][sim]") { runScenario("magias-b"); }
TEST_CASE("Paridade da simulação: técnicas de armas (martelo, manoplas, maça, machado)", "[parity][sim]") { runScenario("armas-a"); }
TEST_CASE("Paridade da simulação: técnicas de armas (adaga, lança, foice, besta)", "[parity][sim]") { runScenario("armas-b"); }
TEST_CASE("Paridade da simulação: derrota na Fronteira e saque da carga", "[parity][sim]") { runScenario("derrota-fronteira"); }
TEST_CASE("Paridade da simulação: guarda levanta o jogador derrubado", "[parity][sim]") { runScenario("resgate"); }
TEST_CASE("Paridade da simulação: derrota em Full Loot", "[parity][sim]") { runScenario("fullloot"); }
TEST_CASE("Paridade da simulação: mercado, fabricação, reparo e treino", "[parity][sim]") { runScenario("economia"); }
TEST_CASE("Paridade da simulação: acampamento reativo", "[parity][sim]") { runScenario("acampamento"); }
TEST_CASE("Paridade da simulação: incursão na Turbulenta", "[parity][sim]") { runScenario("turbulenta"); }
TEST_CASE("Paridade da simulação: estrelas e observação", "[parity][sim]") { runScenario("estrelas"); }
TEST_CASE("Paridade da simulação: coleta e rebrota", "[parity][sim]") { runScenario("coleta"); }

TEST_CASE("JsMath: sin, cos, atan2 e exp iguais aos do V8 em 1 milhão de argumentos", "[parity][jsmath]") {
  const json& jm = fixture()["jsMath"];
  rpg::Mulberry32 R(jm["seed"].get<std::int32_t>());
  const int n = jm["n"];
  std::uint32_t h[4] = {0x811c9dc5u, 0x811c9dc5u, 0x811c9dc5u, 0x811c9dc5u};
  const auto mix = [&](int k, double v) {
    unsigned char b[8];
    std::memcpy(b, &v, 8);
    for (const unsigned char c : b) {
      h[k] ^= c;
      h[k] *= 0x01000193u;
    }
  };
  for (int i = 0; i < n; ++i) {
    const int k = i % 4;
    const double x = k == 0 ? (R.next() - 0.5) * 20 : k == 1 ? (R.next() - 0.5) * 2000 : k == 2 ? (R.next() - 0.5) * 1e-3 : (R.next() - 0.5) * 1e6;
    const double y = (R.next() - 0.5) * (k == 3 ? 1e4 : 20);
    const double e = (R.next() - 0.5) * 40;
    mix(0, rpg::js::sin(x));
    mix(1, rpg::js::cos(x));
    mix(2, rpg::js::atan2(x, y));
    mix(3, rpg::js::exp(e));
  }
  const auto hex = [](std::uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", v);
    return std::string(buf);
  };
  CHECK(hex(h[0]) == jm["hashes"]["sin"].get<std::string>());
  CHECK(hex(h[1]) == jm["hashes"]["cos"].get<std::string>());
  CHECK(hex(h[2]) == jm["hashes"]["atan2"].get<std::string>());
  CHECK(hex(h[3]) == jm["hashes"]["exp"].get<std::string>());
}
