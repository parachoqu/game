#include "core/data/GameData.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace rpg {
namespace {

// ordered_json preserva a ordem das chaves do arquivo, que é a ordem de declaração do config.js.
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

Json readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw DataError("não foi possível abrir " + path.string());
  try {
    return Json::parse(in);
  } catch (const Json::exception& e) {
    throw DataError(path.filename().string() + ": JSON inválido: " + e.what());
  }
}

// Leitura de campos com o caminho do erro ("items.json › espada › weight").
class Node {
 public:
  Node(const Json& j, std::string where) : j_(j), where_(std::move(where)) {}

  const Json& json() const { return j_; }
  const std::string& where() const { return where_; }
  Node child(const std::string& key) const {
    if (!j_.is_object() || !j_.contains(key)) fail(key, "campo obrigatório ausente");
    return {j_.at(key), where_ + " › " + key};
  }
  bool has(const std::string& key) const { return j_.is_object() && j_.contains(key) && !j_.at(key).is_null(); }

  double number(const std::string& key) const {
    const Node n = child(key);
    if (!n.j_.is_number()) fail(key, "esperado número");
    return n.j_.get<double>();
  }
  std::optional<double> optNumber(const std::string& key) const {
    if (!has(key)) return std::nullopt;
    return number(key);
  }
  int integer(const std::string& key) const {
    const Node n = child(key);
    if (!n.j_.is_number_integer()) fail(key, "esperado inteiro");
    return n.j_.get<int>();
  }
  std::string string(const std::string& key) const {
    const Node n = child(key);
    if (!n.j_.is_string()) fail(key, "esperado texto");
    return n.j_.get<std::string>();
  }
  std::optional<std::string> optString(const std::string& key) const {
    if (!has(key)) return std::nullopt;
    return string(key);
  }
  bool boolean(const std::string& key, bool fallback) const {
    if (!has(key)) return fallback;
    const Node n = child(key);
    if (!n.j_.is_boolean()) fail(key, "esperado verdadeiro/falso");
    return n.j_.get<bool>();
  }

  [[noreturn]] void fail(const std::string& key, const std::string& msg) const {
    throw DataError(where_ + " › " + key + ": " + msg);
  }
  [[noreturn]] void fail(const std::string& msg) const { throw DataError(where_ + ": " + msg); }

 private:
  const Json& j_;
  std::string where_;
};

// Percorre as entradas de um objeto na ordem do arquivo.
template <class F>
void forEachEntry(const Json& obj, const std::string& file, F&& f) {
  if (!obj.is_object()) throw DataError(file + ": esperado objeto na raiz");
  for (const auto& [key, value] : obj.items()) f(key, Node(value, file + " › " + key));
}

ItemKind parseItemKind(const Node& n) {
  const std::string s = n.string("kind");
  if (s == "material") return ItemKind::Material;
  if (s == "goods") return ItemKind::Goods;
  if (s == "consumable") return ItemKind::Consumable;
  if (s == "weapon") return ItemKind::Weapon;
  if (s == "armor") return ItemKind::Armor;
  if (s == "bag") return ItemKind::Bag;
  if (s == "mount") return ItemKind::Mount;
  n.fail("kind", "tipo de item desconhecido '" + s + "'");
}

AttackType parseAttackType(const Node& n) {
  const std::string s = n.string("type");
  if (s == "melee") return AttackType::Melee;
  if (s == "ranged") return AttackType::Ranged;
  if (s == "spell") return AttackType::Spell;
  n.fail("type", "tipo de ataque desconhecido '" + s + "'");
}

SpellSchool parseSchool(const Node& n) {
  const auto s = n.optString("school");
  if (!s) return SpellSchool::None;
  if (*s == "ice") return SpellSchool::Ice;
  if (*s == "fire") return SpellSchool::Fire;
  if (*s == "nature") return SpellSchool::Nature;
  if (*s == "wind") return SpellSchool::Wind;
  if (*s == "curse") return SpellSchool::Curse;
  if (*s == "vital") return SpellSchool::Vital;
  if (*s == "holy") return SpellSchool::Holy;
  n.fail("school", "escola desconhecida '" + *s + "'");
}

Faction parseFaction(const Node& n) {
  const std::string s = n.string("faction");
  if (s == "beast") return Faction::Beast;
  if (s == "bandit") return Faction::Bandit;
  if (s == "shadow") return Faction::Shadow;
  if (s == "guard") return Faction::Guard;
  n.fail("faction", "facção desconhecida '" + s + "'");
}

LossRule parseLoss(const Node& n) {
  const std::string s = n.string("loss");
  if (s == "wear") return LossRule::Wear;
  if (s == "cargo") return LossRule::Cargo;
  if (s == "full") return LossRule::Full;
  n.fail("loss", "regra de perda desconhecida '" + s + "'");
}

// Tabela de preços { item: preço } → vetor indexado por ItemId.
std::vector<std::optional<double>> parsePrices(const Node& n, const DefTable<ItemDef, ItemId>& items) {
  std::vector<std::optional<double>> out(items.size());
  if (!n.json().is_object()) n.fail("esperado objeto { item: preço }");
  for (const auto& [key, value] : n.json().items()) {
    const ItemId id = items.require(key, n.where());
    if (!value.is_number()) n.fail(key, "esperado número");
    out[id.value] = value.get<double>();
  }
  return out;
}

void loadBalance(GameData& d, const fs::path& dir) {
  const Json j = readFile(dir / "balance.json");
  const Node root(j, "balance.json");
  const Node clock = root.child("clock");
  d.balance.clock.dayLength = clock.number("dayLength");
  d.balance.clock.startHour = clock.number("startHour");
  if (d.balance.clock.dayLength <= 0) clock.fail("dayLength", "precisa ser positivo");

  const Node mv = root.child("movement");
  auto& m = d.balance.movement;
  m.walk = mv.number("walk");
  m.run = mv.number("run");
  m.crouch = mv.number("crouch");
  m.gravity = mv.number("gravity");
  m.airControl = mv.number("airControl");
  const Node jump = mv.child("jump");
  m.jump.vy = jump.number("vy");
  m.jump.vigor = jump.number("vigor");
  const Node boost = mv.child("boost");
  m.boost.vy = boost.number("vy");
  m.boost.vigor = boost.number("vigor");
  m.boost.speed = boost.number("speed");

  const Node col = root.child("collision");
  d.balance.collision = {col.number("cell"), col.number("maxSlope"), col.number("routeSlope"), col.number("routeLane")};
  const Node inv = root.child("inventory");
  d.balance.inventory = {inv.number("baseCapacity"), inv.number("overLimit")};
  d.balance.turbulent.duration = root.child("turbulent").number("duration");
}

}  // namespace

GameData GameData::load(const fs::path& dir) {
  GameData d;
  loadBalance(d, dir);

  forEachEntry(readFile(dir / "trainings.json"), "trainings.json", [&](const std::string& key, const Node& n) {
    d.trainings.add(key, TrainingDef{n.integer("cost")});
  });

  forEachEntry(readFile(dir / "skills.json"), "skills.json", [&](const std::string& key, const Node& n) {
    d.skills.add(key, SkillDef{n.number("cost"), n.number("cd")});
  });

  forEachEntry(readFile(dir / "weapons.json"), "weapons.json", [&](const std::string& key, const Node& n) {
    WeaponDef w;
    const Node b = n.child("basic");
    w.basic.type = parseAttackType(b);
    w.basic.damage = b.number("dmg");
    w.basic.windup = b.number("windup");
    w.basic.recover = b.number("recover");
    w.basic.school = parseSchool(b);
    if (w.basic.type == AttackType::Melee) {
      w.basic.range = b.number("range");
      w.basic.arc = b.number("arc");
    } else {
      w.basic.speed = b.number("speed");
    }
    const Node skills = n.child("skills");
    if (!skills.json().is_array()) skills.fail("esperada lista");
    for (const auto& s : skills.json()) {
      if (!s.is_string()) skills.fail("esperada lista de chaves de técnica");
      w.skills.push_back(d.skills.require(s.get<std::string>(), skills.where()));
    }
    d.weapons.add(key, std::move(w));
  });

  forEachEntry(readFile(dir / "items.json"), "items.json", [&](const std::string& key, const Node& n) {
    ItemDef it;
    it.weight = n.number("weight");
    it.kind = parseItemKind(n);
    if (const auto fam = n.optString("family")) it.family = d.weapons.require(*fam, n.where() + " › family");
    if (const auto req = n.optString("req")) it.requiredTraining = d.trainings.require(*req, n.where() + " › req");
    if (it.kind == ItemKind::Weapon && !it.family) n.fail("arma sem `family`");
    if (it.kind == ItemKind::Armor) {
      it.armorHp = n.integer("hp");
      it.armorSpeed = n.number("speed");
    }
    if (it.kind == ItemKind::Bag || it.kind == ItemKind::Mount) it.capacity = n.number("cap");
    d.items.add(key, it);
  });

  forEachEntry(readFile(dir / "enemies.json"), "enemies.json", [&](const std::string& key, const Node& n) {
    EnemyDef e;
    e.faction = parseFaction(n);
    e.hp = n.number("hp");
    e.speed = n.number("speed");
    e.damage = n.number("dmg");
    e.range = n.number("range");
    if (const auto arc = n.optNumber("arc")) e.arc = *arc;
    if (const auto shape = n.optString("shape")) {
      if (*shape != "circle") n.fail("shape", "forma desconhecida '" + *shape + "'");
      e.shape = AttackShape::Circle;
    }
    // O guarda tem IA própria e não usa windup/cd/aggro (a definição inline dele não os traz).
    if (e.faction != Faction::Guard) {
      e.windup = n.number("windup");
      e.cooldown = n.number("cd");
      e.aggro = n.number("aggro");
    }
    if (n.has("drop")) {
      const Node drops = n.child("drop");
      for (const auto& [item, range] : drops.json().items()) {
        if (!range.is_array() || range.size() != 2 || !range[0].is_number_integer() || !range[1].is_number_integer())
          drops.fail(item, "esperado [mín, máx] inteiros");
        e.drops.push_back({d.items.require(item, drops.where()), range[0].get<int>(), range[1].get<int>()});
      }
    }
    if (n.has("coins")) {
      const Node c = n.child("coins");
      if (!c.json().is_array() || c.json().size() != 2 || !c.json()[0].is_number_integer() ||
          !c.json()[1].is_number_integer())
        c.fail("esperado [mín, máx] inteiros");
      e.coins = std::pair{c.json()[0].get<int>(), c.json()[1].get<int>()};
    }
    e.ranged = n.boolean("ranged", false);
    if (const auto w = n.optString("weapon")) e.weapon = d.weapons.require(*w, n.where() + " › weapon");
    if (const auto s = n.optNumber("scale")) e.scale = *s;
    d.enemies.add(key, std::move(e));
  });

  forEachEntry(readFile(dir / "zones.json"), "zones.json", [&](const std::string& key, const Node& n) {
    d.zones.add(key, ZoneDef{parseLoss(n)});
  });

  {
    const Json j = readFile(dir / "markets.json");
    const Node root(j, "markets.json");
    d.mountPrice = root.number("mountPrice");
    d.restartKitPrice = root.number("restartKitPrice");
    forEachEntry(root.child("markets").json(), "markets.json › markets", [&](const std::string& key, const Node& n) {
      d.markets.add(key, MarketDef{parsePrices(n.child("buy"), d.items), parsePrices(n.child("sell"), d.items)});
    });
  }

  {
    const Json j = readFile(dir / "recipes.json");
    if (!j.is_array()) throw DataError("recipes.json: esperada lista na raiz");
    for (std::size_t i = 0; i < j.size(); ++i) {
      const Node n(j[i], "recipes.json › [" + std::to_string(i) + "]");
      RecipeDef r;
      const std::string out = n.string("out");
      r.out = d.items.require(out, n.where() + " › out");
      r.quantity = n.integer("qty");
      const Node in = n.child("in");
      for (const auto& [item, qty] : in.json().items()) {
        if (!qty.is_number_integer()) in.fail(item, "esperado inteiro");
        r.inputs.emplace_back(d.items.require(item, in.where()), qty.get<int>());
      }
      if (const auto req = n.optString("req")) r.requiredTraining = d.trainings.require(*req, n.where() + " › req");
      d.recipes.add(out, std::move(r));  // `out` é único (o exportador confere)
    }
  }

  forEachEntry(readFile(dir / "origins.json"), "origins.json",
               [&](const std::string& key, const Node&) { d.origins.add(key, OriginDef{}); });

  forEachEntry(readFile(dir / "starts.json"), "starts.json", [&](const std::string& key, const Node& n) {
    StartDef s;
    s.weapon = d.items.require(n.string("weapon"), n.where() + " › weapon");
    if (d.items[s.weapon].kind != ItemKind::Weapon) n.fail("weapon", "o item inicial precisa ser uma arma");
    s.training = d.trainings.find(key);
    d.starts.add(key, s);
  });

  return d;
}

}  // namespace rpg
