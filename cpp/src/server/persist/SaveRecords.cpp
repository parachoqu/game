#include "server/persist/SaveRecords.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "core/Log.h"
#include "core/world/ZoneMap.h"
#include "sim/Sim.h"

namespace rpg::server::persist {

namespace {

namespace proto = rpg::protocol;
using sim::InvEntry;
using sim::ItemList;
using sim::Player;
using sim::Sim;

constexpr std::array<std::string_view, 4> kCampKeys{"estabelecido", "pressionado", "deslocado", "recuperacao"};

std::string_view campKey(proto::CampPhase c) { return kCampKeys[static_cast<std::size_t>(c)]; }

std::optional<proto::CampPhase> campFromKey(std::string_view k) {
  for (std::size_t i = 0; i < kCampKeys.size(); ++i)
    if (kCampKeys[i] == k) return static_cast<proto::CampPhase>(i);
  return std::nullopt;
}

// Leitura tolerante: um campo ausente ou de outro tipo fica com o valor padrão (saves editados à mão,
// saves de versões antigas). Só o formato geral é exigido.
double num(const Json& j, const char* key, double def) {
  const auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<double>() : def;
}
bool flag(const Json& j, const char* key, bool def) {
  const auto it = j.find(key);
  return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}
std::string str(const Json& j, const char* key, std::string def = {}) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}
const Json& obj(const Json& j, const char* key) {
  static const Json kEmpty = Json::object();
  const auto it = j.find(key);
  return it != j.end() && it->is_object() ? *it : kEmpty;
}
const Json& arr(const Json& j, const char* key) {
  static const Json kEmpty = Json::array();
  const auto it = j.find(key);
  return it != j.end() && it->is_array() ? *it : kEmpty;
}

// ---------------------------------------------------------------- inventário
Json entryJson(const GameData& d, const InvEntry& e) {
  Json j = Json::object();
  j["id"] = d.items.key(e.id);
  if (e.uid) {
    j["uid"] = e.uid;
    j["cond"] = e.cond;
  } else {
    j["qty"] = e.qty;
  }
  return j;
}

Json listJson(const GameData& d, const ItemList& list) {
  Json j = Json::array();
  for (const InvEntry& e : list) j.push_back(entryJson(d, e));
  return j;
}

// Equipamento ganha uid novo (do mundo em que o personagem entra); pilhas ficam como estão, na ordem.
std::optional<InvEntry> readEntry(Sim& s, const Json& j, int* dropped) {
  if (!j.is_object()) return std::nullopt;
  const ItemId id = s.data.items.find(str(j, "id"));
  if (!id) {
    if (dropped) ++*dropped;
    return std::nullopt;
  }
  if (s.data.items[id].isGear()) return sim::makeGear(s.uids, id, std::clamp(num(j, "cond", 100.0), 0.0, 100.0));
  InvEntry e;
  e.id = id;
  e.qty = static_cast<int>(num(j, "qty", 1));
  if (e.qty <= 0) return std::nullopt;
  return e;
}

ItemList readList(Sim& s, const Json& j, int* dropped) {
  ItemList out;
  if (!j.is_array()) return out;
  for (const Json& e : j)
    if (auto entry = readEntry(s, e, dropped)) out.push_back(*entry);
  return out;
}

std::optional<sim::Gear> readGear(Sim& s, const Json& j, int* dropped) {
  if (!j.is_object()) return std::nullopt;
  auto e = readEntry(s, j, dropped);
  if (!e || !e->uid) return std::nullopt;
  return e;
}

// ---------------------------------------------------------------- Livro
Json argJson(const GameData& d, const proto::MsgArg& a) {
  using T = proto::MsgArg::Type;
  switch (a.type) {
    case T::Int: return {{"int", a.number}};
    case T::Real: return {{"real", a.number}};
    case T::Text: return {{"text", a.text}};
    case T::Item: return {{"item", d.items.key(ItemId{a.id})}};
    case T::Training: return {{"training", d.trainings.key(TrainingId{a.id})}};
    case T::Enemy: return {{"enemy", d.enemies.key(EnemyTypeId{a.id})}};
    case T::Place: return {{"place", placeKey(static_cast<PlaceId>(a.id))}};
    case T::Zone: return {{"zone", zoneKey(static_cast<ZoneKind>(a.id))}};
    case T::Message: return {{"msg", proto::messageKey(static_cast<proto::MessageId>(a.id))}};
  }
  return {{"int", 0}};
}

std::optional<proto::MsgArg> readArg(const GameData& d, const Json& j) {
  using proto::MsgArg;
  if (!j.is_object() || j.size() != 1) return std::nullopt;
  const auto first = *j.items().begin();
  const std::string& k = first.key();
  const Json& v = first.value();
  const auto ref = [](MsgArg::Type t, auto id) -> std::optional<MsgArg> {
    if (!id) return std::nullopt;
    return MsgArg::ref(t, id.value);
  };
  if (k == "int" && v.is_number()) return MsgArg::integer(static_cast<long long>(v.get<double>()));
  if (k == "real" && v.is_number()) return MsgArg::real(v.get<double>());
  if (k == "text" && v.is_string()) return MsgArg::str(v.get<std::string>());
  if (!v.is_string()) return std::nullopt;
  const std::string key = v.get<std::string>();
  if (k == "item") return ref(MsgArg::Type::Item, d.items.find(key));
  if (k == "training") return ref(MsgArg::Type::Training, d.trainings.find(key));
  if (k == "enemy") return ref(MsgArg::Type::Enemy, d.enemies.find(key));
  if (k == "place") {
    const auto p = placeFromKey(key);
    if (!p) return std::nullopt;
    return MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(*p));
  }
  if (k == "zone") {
    const auto z = zoneFromKey(key);
    if (!z) return std::nullopt;
    return MsgArg::ref(MsgArg::Type::Zone, static_cast<std::uint16_t>(*z));
  }
  if (k == "msg") {
    const auto m = proto::messageFromKey(key);
    if (!m) return std::nullopt;
    return MsgArg::message(*m);
  }
  return std::nullopt;
}

Json bookJson(const GameData& d, const sim::Book& book) {
  Json entries = Json::array();
  for (const proto::BookEntry& e : book.entries()) {
    Json j = Json::object();
    j["id"] = e.id;
    j["time"] = e.time;
    j["day"] = e.day;
    j["key"] = e.key;
    j["cat"] = proto::bookCatKey(e.cat);
    j["domain"] = e.domain == proto::BookDomain::None ? Json(nullptr) : Json(proto::bookDomainKey(e.domain));
    j["count"] = e.count;
    j["public"] = e.isPublic;
    if (e.personal) j["personal"] = true;
    if (e.updatedTime) j["updatedTime"] = *e.updatedTime;
    if (!e.data.empty()) {
      Json data = Json::object();
      for (const auto& [k, v] : e.data) data[k] = v;
      j["data"] = std::move(data);
    }
    if (e.literal) {
      j["title"] = e.literalTitle;
      j["text"] = e.literalText;
    } else {
      j["tmpl"] = e.tmpl;
      Json args = Json::array();
      for (const proto::MsgArg& a : e.args) args.push_back(argJson(d, a));
      j["args"] = std::move(args);
    }
    entries.push_back(std::move(j));
  }
  Json titles = Json::array();
  for (const proto::BookTitle& t : book.titles()) {
    Json j = {{"id", t.id}};
    if (!t.literalName.empty()) {
      j["name"] = t.literalName;
      j["why"] = t.literalWhy;
    }
    titles.push_back(std::move(j));
  }
  return {{"entries", std::move(entries)},
          {"titles", std::move(titles)},
          {"shownTitle", book.shownTitle() ? Json(*book.shownTitle()) : Json(nullptr)},
          {"seq", book.seq()}};
}

void readBook(const GameData& d, const Json& j, sim::Book& book) {
  book.clear();
  int maxId = 0;
  for (const Json& je : arr(j, "entries")) {
    if (!je.is_object()) continue;
    proto::BookEntry e;
    e.id = static_cast<int>(num(je, "id", 0));
    e.time = num(je, "time", 0);
    e.day = static_cast<int>(num(je, "day", 1));
    e.key = str(je, "key");
    e.cat = proto::bookCatFromKey(str(je, "cat")).value_or(proto::BookCat::Historia);
    e.domain = proto::bookDomainFromKey(str(je, "domain")).value_or(proto::BookDomain::None);
    e.count = static_cast<int>(num(je, "count", 1));
    e.isPublic = flag(je, "public", false);
    e.personal = flag(je, "personal", false);
    if (const auto it = je.find("updatedTime"); it != je.end() && it->is_number()) e.updatedTime = it->get<double>();
    for (const auto& [k, v] : obj(je, "data").items())
      if (v.is_number()) e.data.emplace_back(k, v.get<double>());
    if (je.contains("tmpl")) {
      e.tmpl = str(je, "tmpl");
      for (const Json& a : arr(je, "args")) {
        if (auto arg = readArg(d, a)) e.args.push_back(std::move(*arg));
        else e.args.push_back(proto::MsgArg::str("?"));  // referência que sumiu dos dados
      }
    } else {
      e.literal = true;
      e.literalTitle = str(je, "title");
      e.literalText = str(je, "text");
    }
    maxId = std::max(maxId, e.id);
    book.entries().push_back(std::move(e));
  }
  for (const Json& jt : arr(j, "titles")) {
    if (!jt.is_object() || str(jt, "id").empty()) continue;
    book.titles().push_back({str(jt, "id"), str(jt, "name"), str(jt, "why")});
  }
  if (const auto it = j.find("shownTitle"); it != j.end() && it->is_string()) book.setShownTitle(it->get<std::string>());
  book.setSeq(std::max(static_cast<int>(num(j, "seq", 1)), maxId + 1));
  book.touch();
}

}  // namespace

// ---------------------------------------------------------------- posição
XZ safeSpawn(const StaticWorld& w) { return w.layout().shelter; }

bool isValidPosition(const StaticWorld& w, double x, double z) {
  if (!std::isfinite(x) || !std::isfinite(z)) return false;
  if (w.mapAtDemoX(x) != MapKind::Region) return false;
  const StaticMap& m = w.map(MapKind::Region);
  const double lim = m.half() - 12;
  if (std::abs(x - m.center().x) > lim || std::abs(z - m.center().z) > lim) return false;
  if (m.collision().testPoint(x, z, 0.6)) return false;
  if (m.waterAt(x, z) - m.groundHeight(x, z) > 0.8) return false;
  return true;
}

bool canSave(const Player& p) { return p.map == MapKind::Region && !p.turb.inside; }

// ---------------------------------------------------------------- exportação
Json exportCharacter(const Sim& s, const Player& p) {
  const GameData& d = s.data;
  const auto round = [](double v, double k) { return std::round(v * k) / k; };
  Json equip = Json::object();
  const auto gear = [&](const char* k, const std::optional<sim::Gear>& g) {
    equip[k] = g ? entryJson(d, *g) : Json(nullptr);
  };
  gear("weapon", p.equip.weapon);
  gear("armor", p.equip.armor);
  gear("bag", p.equip.bag);
  gear("mount", p.equip.mount);

  Json stats = Json::object();
  for (const auto& [k, v] : p.stats) stats[k] = v;
  Json flags = Json::array();
  for (const std::string& f : p.flags) flags.push_back(f);
  Json observed = Json::array();
  for (const int o : p.observed) observed.push_back(o);
  Json contrib = Json::object();
  for (const auto& [k, v] : p.evt.contrib) contrib[k] = v;
  Json delivered = Json::object();
  for (const auto& [k, v] : p.evt.delivered) delivered[k] = v;

  Json j = Json::object();
  j["kind"] = "character";
  j["v"] = kCharacterVersion;
  j["world"] = {{"layout", s.statics.worldLayoutVersion()},
                {"pos", {round(p.pos.x, 100), round(p.pos.z, 100)}},
                {"yaw", round(p.yaw, 1000)}};
  j["profile"] = {{"name", p.name}, {"origin", p.origin}, {"start", p.start}, {"model", p.model}};
  j["P"] = {{"coins", p.coins},
            {"trainings", p.trainings},
            {"inv", listJson(d, p.inv)},
            {"saddle", listJson(d, p.saddle)},
            {"storage", listJson(d, p.storage)},
            {"equip", std::move(equip)},
            {"stableBags", listJson(d, p.stableBags)}};
  j["stats"] = std::move(stats);
  j["flags"] = std::move(flags);
  j["sky"] = {{"observed", std::move(observed)}};
  j["evt"] = {{"contrib", std::move(contrib)}, {"pts", p.evt.pts}, {"delivered", std::move(delivered)}};
  j["book"] = bookJson(d, p.book);
  return j;
}

Json exportWorld(const Sim& s) {
  const GameData& d = s.data;
  Json mkt = Json::object();
  Json base = Json::object();
  const ItemId tools = d.items.find("ferramentas");
  for (const MarketId mk : d.markets.ids()) {
    Json demand = Json::object();
    for (const auto& [id, v] : s.demand[mk.value]) demand[d.items.key(id)] = v;
    mkt[d.markets.key(mk)] = {{"demand", std::move(demand)}};
    if (tools) {
      const auto& price = s.sell[mk.value][tools.value];
      base[d.markets.key(mk)] = price ? Json(*price) : Json(nullptr);
    }
  }
  Json vanished = Json::array();
  for (const sim::VanishedStar& v : s.skyState.vanished)
    vanished.push_back({{"index", v.index}, {"constellation", v.constellation}, {"time", v.time}});

  Json j = Json::object();
  j["kind"] = "world";
  j["v"] = kWorldVersion;
  j["time"] = s.time;
  j["evt"] = {{"progress", s.evt.progress},
              {"done", s.evt.done},
              {"doneAt", s.evt.doneAt ? Json(*s.evt.doneAt) : Json(nullptr)},
              {"othersPts", s.evt.othersPts},
              {"othersT", s.evt.othersT},
              {"playersPts", s.evt.playersPts}};
  j["camp"] = {{"pop", s.camp.pop}, {"state", campKey(s.camp.state)}};
  j["sky"] = {{"vanished", std::move(vanished)}, {"instrumentGiven", s.skyState.instrumentGiven}};
  j["mkt"] = std::move(mkt);
  j["marketBase"] = std::move(base);
  j["uid"] = s.uids.peek();
  return j;
}

// ---------------------------------------------------------------- migração (save.js migrateSave)
std::optional<Json> migrateCharacter(const StaticWorld& w, Json d, Migration* out) {
  if (!d.is_object() || !d.contains("P") || !d["P"].is_object() || !d.contains("profile") || !d["profile"].is_object())
    return std::nullopt;
  Migration m;
  m.fromVersion = static_cast<int>(num(d, "v", 1));
  const int layout = w.worldLayoutVersion();

  if (!d.contains("world") || !d["world"].is_object()) {
    d["world"] = {{"layout", 1}, {"pos", nullptr}, {"yaw", 0}};
    m.noPosition = true;
    m.notes.emplace_back("save do recorte anterior: sem posição gravada");
  }
  Json& world = d["world"];
  m.fromLayout = static_cast<int>(num(world, "layout", 1));
  m.toLayout = layout;
  if (m.fromLayout != layout) {
    m.layoutChanged = true;
    // As coordenadas do recorte de 400 × 400 não têm equivalente no mundo de 1.024 × 1.024: qualquer
    // conversão por escala cairia dentro de construções, no rio ou fora do mapa.
    world["pos"] = nullptr;
    world["layout"] = layout;
    m.notes.push_back("layout do mundo " + std::to_string(m.fromLayout) + " → " + std::to_string(layout) +
                      ": posição reposicionada");
  }
  const Json& pos = world["pos"];
  const bool hasPos = pos.is_array() && pos.size() == 2 && pos[0].is_number() && pos[1].is_number();
  if (hasPos && !isValidPosition(w, pos[0].get<double>(), pos[1].get<double>())) {
    world["pos"] = nullptr;
    m.invalidPosition = true;
    m.notes.emplace_back("posição gravada inválida no mundo atual");
  } else if (!hasPos && !world["pos"].is_null()) {
    world["pos"] = nullptr;
  }
  if (world["pos"].is_null()) {
    const XZ s = safeSpawn(w);
    world["pos"] = {s.x, s.z};
    m.repositioned = true;
  }
  if (!world.contains("yaw") || !world["yaw"].is_number()) world["yaw"] = kPi;
  d["kind"] = "character";
  d["v"] = kCharacterVersion;
  if (out) *out = std::move(m);
  return d;
}

sim::PlayerProfile profileOf(const GameData& data, const Json& rec) {
  const Json& p = obj(rec, "profile");
  sim::PlayerProfile out;
  out.name = str(p, "name").substr(0, 32);
  if (out.name.empty()) out.name = "Viajante";
  out.origin = str(p, "origin");
  if (!data.origins.find(out.origin)) out.origin = "humano";
  out.start = str(p, "start");
  if (!data.starts.find(out.start)) out.start = "espadachim";
  out.model = str(p, "model");
  return out;
}

// ---------------------------------------------------------------- aplicação (save.js applySave)
sim::EntityId loadCharacter(sim::World& w, const Json& rec, std::uint32_t session, int* dropped) {
  Sim& s = w.state();
  const GameData& d = s.data;
  const sim::EntityId id = w.addPlayer(profileOf(d, rec), session, false);
  Player& p = *s.player(id);
  const Json& P = obj(rec, "P");

  p.coins = num(P, "coins", p.coins);
  std::vector<std::string> trainings;
  for (const Json& t : arr(P, "trainings"))
    if (t.is_string() && d.trainings.find(t.get<std::string>())) trainings.push_back(t.get<std::string>());
  if (!trainings.empty()) p.trainings = std::move(trainings);
  p.inv = readList(s, P.value("inv", Json::array()), dropped);
  p.saddle = readList(s, P.value("saddle", Json::array()), dropped);
  p.storage = readList(s, P.value("storage", Json::array()), dropped);
  p.stableBags = readList(s, P.value("stableBags", Json::array()), dropped);
  const Json& equip = obj(P, "equip");
  const auto gear = [&](const char* k) -> std::optional<sim::Gear> {
    const auto it = equip.find(k);
    return it == equip.end() ? std::nullopt : readGear(s, *it, dropped);
  };
  p.equip.weapon = gear("weapon");
  p.weaponShown = p.equip.weapon && p.equip.weapon->cond > 0;  // como ao reaparecer
  p.equip.armor = gear("armor");
  p.equip.bag = gear("bag");
  p.equip.mount = gear("mount");

  p.stats.clear();
  for (const auto& [k, v] : obj(rec, "stats").items())
    if (v.is_number()) p.stats.emplace_back(k, v.get<double>());
  p.flags.clear();
  if (const auto it = rec.find("flags"); it != rec.end()) {
    if (it->is_array()) {
      for (const Json& f : *it)
        if (f.is_string()) p.flags.insert(f.get<std::string>());
    } else if (it->is_object()) {  // G.flags da demo: {chave: true}
      for (const auto& [k, v] : it->items())
        if (v.is_boolean() ? v.get<bool>() : !v.is_null()) p.flags.insert(k);
    }
  }
  p.observed.clear();
  for (const Json& o : arr(obj(rec, "sky"), "observed"))
    if (o.is_number()) p.observed.insert(static_cast<int>(o.get<double>()));

  const Json& evt = obj(rec, "evt");
  p.evt = {};
  for (const auto& [k, v] : obj(evt, "contrib").items())
    if (v.is_number()) p.evt.contrib[k] = v.get<double>();
  p.evt.pts = num(evt, "pts", 0);
  for (const auto& [k, v] : obj(evt, "delivered").items())
    if (v.is_number()) p.evt.delivered[k] = static_cast<int>(v.get<double>());

  readBook(d, obj(rec, "book"), p.book);

  const Json& world = obj(rec, "world");
  XZ pos = safeSpawn(s.statics);
  if (const auto it = world.find("pos"); it != world.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() &&
                                         (*it)[1].is_number())
    pos = {(*it)[0].get<double>(), (*it)[1].get<double>()};
  p.map = MapKind::Region;
  p.pos = {pos.x, s.mapOf(MapKind::Region).groundHeight(pos.x, pos.z), pos.z};
  p.yaw = p.aimYaw = num(world, "yaw", p.yaw);
  p.zone = sim::zoneOf(s, MapKind::Region, pos.x, pos.z);
  p.place = sim::placeOf(s, MapKind::Region, pos.x, pos.z);
  p.maxHp = 100 + sim::armorHp(s, p);
  p.hp = p.maxHp;
  return id;
}

void loadWorld(sim::World& w, const Json& rec) {
  Sim& s = w.state();
  const GameData& d = s.data;
  if (!rec.is_object()) return;
  s.time = num(rec, "time", s.time);

  const Json& evt = obj(rec, "evt");
  const bool wasDone = s.evt.done;
  s.evt.progress = num(evt, "progress", 0);
  s.evt.done = flag(evt, "done", false);
  if (const auto it = evt.find("doneAt"); it != evt.end() && it->is_number()) s.evt.doneAt = it->get<double>();
  else s.evt.doneAt.reset();
  s.evt.othersPts = num(evt, "othersPts", 0);
  s.evt.othersT = num(evt, "othersT", 0);
  s.evt.playersPts = num(evt, "playersPts", 0);
  if (s.evt.done && !wasDone) sim::applyReopenEffects(s);

  const Json& camp = obj(rec, "camp");
  s.camp.pop = static_cast<int>(num(camp, "pop", s.camp.pop));
  s.camp.state = campFromKey(str(camp, "state")).value_or(s.camp.state);
  s.camp.since = s.time;
  s.camp.pending = true;

  const Json& sky = obj(rec, "sky");
  s.skyState.vanished.clear();
  for (const Json& v : arr(sky, "vanished")) {
    if (!v.is_object()) continue;
    s.skyState.vanished.push_back({static_cast<int>(num(v, "index", 0)), static_cast<int>(num(v, "constellation", -1)),
                                   num(v, "time", 0)});
  }
  s.skyState.instrumentGiven = flag(sky, "instrumentGiven", false);
  s.skyState.next = s.time + 60;

  const Json& mkt = obj(rec, "mkt");
  const Json& base = obj(rec, "marketBase");
  const ItemId tools = d.items.find("ferramentas");
  for (const MarketId mk : d.markets.ids()) {
    const std::string& key = d.markets.key(mk);
    auto& demand = s.demand[mk.value];
    demand.clear();
    for (const auto& [k, v] : obj(obj(mkt, key.c_str()), "demand").items()) {
      const ItemId id = d.items.find(k);
      if (id && v.is_number()) demand[id] = v.get<double>();
    }
    if (const auto it = base.find(key); tools && !s.evt.done && it != base.end() && it->is_number())
      s.sell[mk.value][tools.value] = it->get<double>();
  }
  s.uids.atLeast(static_cast<std::uint32_t>(num(rec, "uid", 1)));
}

// ---------------------------------------------------------------- importação do save da demo
std::optional<ImportedSave> importDemoSave(const Json& demo) {
  if (!demo.is_object() || !demo.contains("P") || !demo["P"].is_object() || !demo.contains("profile")) return std::nullopt;
  ImportedSave out;

  // personagem: o formato é o da demo, com o Livro em texto pronto
  Json c = Json::object();
  c["kind"] = "character";
  c["v"] = demo.value("v", 1);
  if (demo.contains("world")) c["world"] = demo["world"];
  c["profile"] = demo["profile"];
  c["P"] = demo["P"];
  c["stats"] = obj(demo, "stats");
  Json flags = Json::array();
  for (const auto& [k, v] : obj(demo, "flags").items())
    if (v.is_boolean() ? v.get<bool>() : !v.is_null()) flags.push_back(k);
  c["flags"] = std::move(flags);
  c["sky"] = {{"observed", arr(obj(demo, "sky"), "observed")}};
  const Json& evt = obj(demo, "evt");
  c["evt"] = {{"contrib", obj(evt, "contrib")}, {"pts", num(evt, "playerPts", 0)}, {"delivered", obj(evt, "delivered")}};

  const Json& book = obj(demo, "book");
  Json entries = Json::array();
  for (const Json& e : arr(book, "entries")) {
    if (!e.is_object()) continue;
    Json j = Json::object();
    j["id"] = num(e, "id", 0);
    j["time"] = num(e, "time", 0);
    j["day"] = num(e, "day", 1);
    j["key"] = str(e, "key");
    j["cat"] = str(e, "cat", "historia");
    j["domain"] = e.contains("domain") && e["domain"].is_string() ? e["domain"] : Json(nullptr);
    j["count"] = num(e, "count", 1);
    j["public"] = flag(e, "public", false);
    if (flag(e, "personal", false)) j["personal"] = true;
    if (e.contains("data") && e["data"].is_object()) j["data"] = e["data"];
    j["title"] = str(e, "title");
    j["text"] = str(e, "text");
    entries.push_back(std::move(j));
  }
  Json titles = Json::array();
  for (const Json& t : arr(book, "titles"))
    if (t.is_object() && !str(t, "id").empty())
      titles.push_back({{"id", str(t, "id")}, {"name", str(t, "name")}, {"why", str(t, "why")}});
  c["book"] = {{"entries", std::move(entries)},
               {"titles", std::move(titles)},
               {"shownTitle", book.contains("shownTitle") ? book["shownTitle"] : Json(nullptr)},
               {"seq", num(book, "seq", 1)}};
  out.character = std::move(c);

  // mundo
  Json w = Json::object();
  w["kind"] = "world";
  w["v"] = kWorldVersion;
  w["time"] = num(demo, "time", 0);
  w["evt"] = {{"progress", num(evt, "progress", 0)},
              {"done", flag(evt, "done", false)},
              {"doneAt", evt.contains("doneAt") ? evt["doneAt"] : Json(nullptr)},
              {"othersPts", num(evt, "othersPts", 0)},
              {"othersT", num(evt, "othersT", 0)},
              {"playersPts", num(evt, "playerPts", 0)}};
  w["camp"] = obj(demo, "camp");
  const Json& sky = obj(demo, "sky");
  Json vanished = Json::array();
  for (const Json& v : arr(sky, "vanished"))
    if (v.is_object())
      vanished.push_back({{"index", num(v, "index", 0)}, {"constellation", num(v, "constellation", -1)}, {"time", num(v, "time", 0)}});
  w["sky"] = {{"vanished", std::move(vanished)}, {"instrumentGiven", flag(sky, "instrumentGiven", false)}};
  w["mkt"] = obj(demo, "mkt");
  w["marketBase"] = obj(demo, "marketBase");
  w["uid"] = 1;
  out.world = std::move(w);
  return out;
}

}  // namespace rpg::server::persist
