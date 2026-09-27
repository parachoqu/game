#include "sim/Sim.h"

#include <algorithm>

#include "core/Math.h"

namespace rpg::sim {

using protocol::MsgArg;

Sim::Sim(const GameData& d, const StaticWorld& s, const GameplayLayout& l, std::unique_ptr<IRandom> r, SimOptions o)
    : data(d), statics(s), layout(l), sky(SkyLayout::generate()), rng(std::move(r)), opts(o) {
  demand.resize(data.markets.size());
  for (const MarketId mk : data.markets.ids()) sell.push_back(data.markets[mk].sell);
  nodes.resize(layout.nodes.size());
}

Player* Sim::player(EntityId id) {
  for (auto& p : players)
    if (p->id == id) return p.get();
  return nullptr;
}

const Player* Sim::player(EntityId id) const { return const_cast<Sim*>(this)->player(id); }
const Enemy* Sim::enemy(EntityId id) const { return const_cast<Sim*>(this)->enemy(id); }

Enemy* Sim::enemy(EntityId id) {
  if (id == kNoEntity) return nullptr;
  for (auto& e : enemies)
    if (e->id == id) return e.get();
  return nullptr;
}

LootBag* Sim::lootBag(EntityId id) {
  if (id == kNoEntity) return nullptr;
  for (auto& b : lootBags)
    if (b->id == id) return b.get();
  return nullptr;
}

const Npc* Sim::npc(EntityId id) const {
  for (const Npc& n : npcs)
    if (n.id == id) return &n;
  return nullptr;
}

Player* Sim::nearestPlayer(MapKind map, InstanceId inst, double x, double z, double* dist) {
  Player* best = nullptr;
  double bd = 0;
  for (auto& p : players) {
    if (p->map != map || p->instance != inst) continue;
    const double d = jsHypot(p->pos.x - x, p->pos.z - z);
    if (!best || d < bd) {
      best = p.get();
      bd = d;
    }
  }
  if (dist) *dist = best ? bd : std::numeric_limits<double>::infinity();
  return best;
}

int Sim::day() const { return clockAt(time, data.balance.clock).day; }

void Sim::toPlayer(const Player& p, protocol::GameEvent e) {
  Recipient r;
  r.type = Recipient::Type::Player;
  r.player = p.id;
  emit(r, std::move(e));
}

void Sim::toNear(MapKind map, InstanceId inst, double x, double z, protocol::GameEvent e, double radius) {
  Recipient r;
  r.type = Recipient::Type::Near;
  r.map = map;
  r.instance = inst;
  r.x = x;
  r.z = z;
  r.radius = radius;
  emit(r, std::move(e));
}

void Sim::sfx(const Player& p, protocol::SoundId snd) { toPlayer(p, protocol::EvSound{snd}); }

void Sim::sfxAt(MapKind map, InstanceId inst, double x, double z, protocol::SoundId snd) {
  toNear(map, inst, x, z, protocol::EvSound{snd});
}

void Sim::toast(const Player& p, protocol::MessageId m, protocol::MsgArgs args, protocol::ToastKind k,
                std::uint16_t ms) {
  toPlayer(p, protocol::EvToast{m, std::move(args), k, ms});
}

void Sim::floatText(MapKind map, InstanceId inst, double x, double y, double z, protocol::FloatKind k,
                    protocol::MessageId m, protocol::MsgArgs args) {
  toNear(map, inst, x, z, protocol::EvFloatText{x, y, z, k, m, std::move(args)});
}

void Sim::burst(MapKind map, InstanceId inst, double x, double y, double z, std::uint32_t color, int n, double speed) {
  toNear(map, inst, x, z, protocol::EvBurst{x, y, z, color, static_cast<std::uint8_t>(n), speed});
}

std::uint32_t Sim::telegraph(MapKind map, InstanceId inst, protocol::EvTelegraph t) {
  t.id = nextTelegraph++;
  t.map = static_cast<std::uint8_t>(map);
  const std::uint32_t id = t.id;
  toNear(map, inst, t.x, t.z, t, 120);
  return id;
}

void Sim::cancelTelegraph(MapKind map, InstanceId inst, double x, double z, std::uint32_t& id) {
  if (id) toNear(map, inst, x, z, protocol::EvTelegraphCancel{id}, 120);
  id = 0;
}

void Sim::bookEvent(Player& p, std::optional<int> change) {
  if (!change) return;
  const BookEntry& e = p.book.entries()[static_cast<std::size_t>(*change)];
  toPlayer(p, protocol::EvBookEntry{e, e.count == 1 && !e.updatedTime});
}

// ---------------------------------------------------------------- inventário
double capacity(const Sim& s, const Player& p) {
  const double base = s.data.balance.inventory.baseCapacity;
  return base + (p.equip.bag ? s.data.items[p.equip.bag->id].capacity : 0.0);
}

double saddleCap(const Sim& s, const Player& p) { return p.equip.mount ? s.data.items[p.equip.mount->id].capacity : 0.0; }

double loadRatio(const Sim& s, const Player& p) { return weightOf(s.data, p.inv) / capacity(s, p); }

motor::LoadState loadState(const Sim& s, const Player& p) {
  const double r = loadRatio(s, p);
  return r <= 1 ? motor::LoadState::Normal
         : r <= s.data.balance.inventory.overLimit ? motor::LoadState::Overloaded
                                                   : motor::LoadState::Limit;
}

bool saddleReachable(const Player& p) {
  if (!p.equip.mount || !p.mount.present) return false;
  return p.mounted || jsHypot(p.mount.pos.x - p.pos.x, p.mount.pos.z - p.pos.z) < 7;
}

int receive(Sim& s, Player& p, ItemId id, int qty) {
  const double w = s.data.items[id].weight;
  const double over = s.data.balance.inventory.overLimit;
  int got = 0;
  for (int i = 0; i < qty; ++i) {
    const double bagW = weightOf(s.data, p.inv), cap = capacity(s, p);
    if (bagW + w <= cap) {
      addTo(s.data, s.uids, p.inv, id, 1);
      got++;
      continue;
    }
    if (saddleReachable(p) && weightOf(s.data, p.saddle) + w <= saddleCap(s, p)) {
      addTo(s.data, s.uids, p.saddle, id, 1);
      got++;
      continue;
    }
    if (bagW + w <= cap * over) {
      addTo(s.data, s.uids, p.inv, id, 1);
      got++;
      continue;
    }
    break;
  }
  return got;
}

bool haveAll(const Player& p, const RecipeDef& r, bool useStorage) {
  for (const auto& [id, n] : r.inputs)
    if (count(p.inv, id) + (useStorage ? count(p.storage, id) : 0) < n) return false;
  return true;
}

void consume(Player& p, const RecipeDef& r, bool useStorage) {
  for (const auto& [id, n] : r.inputs) {
    const int got = removeFrom(p.inv, id, n);
    if (got < n && useStorage) removeFrom(p.storage, id, n - got);
  }
}

std::vector<Gear*> equippedList(Player& p) {
  std::vector<Gear*> out;
  for (std::optional<Gear>* g : {&p.equip.weapon, &p.equip.armor, &p.equip.bag, &p.equip.mount})
    if (*g) out.push_back(&**g);
  return out;
}

double armorHp(const Sim& s, const Player& p) {
  const auto& a = p.equip.armor;
  return a && a->cond > 0 ? s.data.items[a->id].armorHp : 0.0;
}

double armorSpeed(const Sim& s, const Player& p) {
  const auto& a = p.equip.armor;
  return a ? s.data.items[a->id].armorSpeed : 1.0;
}

void wear(std::optional<Gear>& g, double amt) {
  if (g) g->cond = std::max(0.0, g->cond - amt);
}

WeaponFamilyId weaponFamily(const Sim& s, const Player& p) {
  const auto& w = p.equip.weapon;
  if (w && w->cond > 0) return s.data.items[w->id].family;
  return s.data.weapons.find("punhos");
}

bool hasTraining(const Player& p, std::string_view key) {
  return std::find(p.trainings.begin(), p.trainings.end(), key) != p.trainings.end();
}

// ---------------------------------------------------------------- estatísticas e Livro
double stat(Player& p, const std::string& key, double n) {
  for (auto& [k, v] : p.stats) {
    if (k == key) {
      v += n;
      return v;
    }
  }
  p.stats.emplace_back(key, n);
  return n;
}

double statOf(const Player& p, std::string_view key) {
  for (const auto& [k, v] : p.stats)
    if (k == key) return v;
  return 0;
}

void bookFirst(Sim& s, Player& p, std::string key, BookCat cat, BookDomain dom, std::string tmpl, protocol::MsgArgs args) {
  s.bookEvent(p, p.book.first(s.time, s.day(), std::move(key), cat, dom, std::move(tmpl), std::move(args)));
}

void bookTally(Sim& s, Player& p, std::string key, BookCat cat, BookDomain dom, std::string tmpl, int inc,
               std::optional<TallyData> data, protocol::MsgArgs args) {
  s.bookEvent(p, p.book.tally(s.time, s.day(), std::move(key), cat, dom, std::move(tmpl), inc, std::move(data),
                              std::move(args)));
}

void bookLog(Sim& s, Player& p, BookCat cat, BookDomain dom, std::string tmpl, protocol::MsgArgs args, std::string key) {
  s.bookEvent(p, p.book.log(s.time, s.day(), cat, dom, std::move(tmpl), std::move(args), std::move(key)));
}

void grantTitle(Sim& s, Player& p, const std::string& id) {
  if (p.book.grantTitle(id)) s.toPlayer(p, protocol::EvBookTitle{id});
}

// ---------------------------------------------------------------- ids
ItemId item(const Sim& s, std::string_view key) { return s.data.items.require(key, "simulação"); }
EnemyTypeId enemyType(const Sim& s, std::string_view key) { return s.data.enemies.require(key, "simulação"); }

}  // namespace rpg::sim
