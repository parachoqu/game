// Mercados locais, fabricação, treinamento e reparo (game/economy.js).
#include <algorithm>
#include <cmath>

#include "core/Math.h"
#include "core/rules/Economy.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

MsgArg itemArg(ItemId id) { return MsgArg::ref(MsgArg::Type::Item, id.value); }

}  // namespace

double demandOf(const Sim& s, MarketId mk, ItemId id) {
  const auto& d = s.demand[mk.value];
  const auto it = d.find(id);
  return it == d.end() ? 1.0 : it->second;
}

std::optional<double> sellPrice(const Sim& s, MarketId mk, ItemId id, const InvEntry* entry) {
  const auto& table = s.sell[mk.value];
  if (id.value >= table.size() || !table[id.value]) return std::nullopt;
  return rules::sellPrice(*table[id.value], demandOf(s, mk, id), entry && entry->uid, entry ? entry->cond : 100);
}

std::optional<double> buyPrice(const Sim& s, MarketId mk, ItemId id) { return s.data.markets[mk].buyPrice(id); }

void updateMarkets(Sim& s, double dt) {
  for (auto& d : s.demand) {
    for (auto it = d.begin(); it != d.end();) {
      it->second = std::min(1.0, it->second + 0.007 * dt);
      if (it->second >= 1) it = d.erase(it);
      else ++it;
    }
  }
}

double sell(Sim& s, Player& p, MarketId mk, ItemId id, int qty) {
  double earned = 0;
  int sold = 0;
  for (int i = 0; i < qty; ++i) {
    auto it = std::find_if(p.inv.begin(), p.inv.end(), [&](const InvEntry& e) { return e.id == id; });
    if (it == p.inv.end()) break;
    const std::optional<double> price = sellPrice(s, mk, id, &*it);
    if (!price) break;
    if (it->uid) p.inv.erase(it);
    else removeFrom(p.inv, id, 1);
    earned += *price;
    sold++;
    s.demand[mk.value][id] = std::max(0.35, demandOf(s, mk, id) - 0.055);
  }
  if (!sold) return 0;
  p.coins += earned;
  stat(p, "earned", earned);
  const std::string& mkKey = s.data.markets.key(mk);
  const std::string& idKey = s.data.items.key(id);
  stat(p, "sold_" + mkKey + "_" + idKey, sold);
  s.sfx(p, SoundId::Coin);
  s.toast(p, MessageId::MarketSold, {MsgArg::integer(sold), itemArg(id), MsgArg::real(earned)}, ToastKind::Coin);
  if (mkKey == "alto" && idKey == "ferramentas") {
    const double total = stat(p, "earned_alto_ferr", earned);
    bookTally(s, p, "entregas-alto", BookCat::Historia, BookDomain::Comercio, "tally.entregas-alto", sold,
              TallyData{{"total", total}});
  }
  if (idKey == "cristal" || idKey == "fragmento") {
    // o mercado vai como chave de texto ("vale", "alto"): o cliente escreve o nome
    bookFirst(s, p, "venda-" + idKey, BookCat::Historia, BookDomain::Comercio, "first.venda", {itemArg(id), MsgArg::str(mkKey)});
  }
  return earned;
}

int buy(Sim& s, Player& p, MarketId mk, ItemId id, int qty) {
  const std::optional<double> price = buyPrice(s, mk, id);
  if (!price) return 0;
  int n = 0;
  for (int i = 0; i < qty; ++i) {
    if (p.coins < *price) {
      s.toast(p, MessageId::CoinsInsufficient, {}, ToastKind::Warn);
      s.sfx(p, SoundId::Deny);
      break;
    }
    if (s.data.items[id].isGear()) {
      addTo(s.data, s.uids, p.inv, id, 1);
      n++;
      p.coins -= *price;
      continue;
    }
    if (!receive(s, p, id, 1)) {
      s.toast(p, MessageId::CargoNoCapacity, {}, ToastKind::Warn);
      s.sfx(p, SoundId::Deny);
      break;
    }
    p.coins -= *price;
    n++;
  }
  if (n) {
    s.sfx(p, SoundId::Coin);
    s.toast(p, MessageId::MarketBought, {MsgArg::integer(n), itemArg(id)}, ToastKind::Coin);
    stat(p, "spent", n * *price);
  }
  return n;
}

void buyMount(Sim& s, Player& p) {
  if (p.equip.mount) {
    s.toast(p, MessageId::MountAlreadyOwned, {}, ToastKind::Info);
    return;
  }
  if (p.coins < s.data.mountPrice) {
    s.toast(p, MessageId::CoinsInsufficient, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  p.coins -= s.data.mountPrice;
  p.equip.mount = makeGear(s.uids, item(s, "montaria"));
  s.sfx(p, SoundId::Coin);
  s.toast(p, MessageId::MountBought, {}, ToastKind::Item);
  bookFirst(s, p, "compra-montaria", BookCat::Historia, BookDomain::Comercio, "first.compra-montaria");
}

void restartKit(Sim& s, Player& p) {
  const double price = p.coins >= s.data.restartKitPrice ? s.data.restartKitPrice : 0;
  p.coins -= price;
  addTo(s.data, s.uids, p.inv, item(s, "martelo"), 1, 70.0);
  addTo(s.data, s.uids, p.inv, item(s, "pocao"), 1);
  if (price != 0.0) s.toast(p, MessageId::KitBought, {MsgArg::real(price)}, ToastKind::Item);
  else s.toast(p, MessageId::KitFree, {}, ToastKind::Item);
  stat(p, "kits");
  bookFirst(s, p, "recomeco", BookCat::Historia, BookDomain::Trabalho, "first.recomeco");
}

void learn(Sim& s, Player& p, TrainingId id) {
  const std::string& key = s.data.trainings.key(id);
  if (hasTraining(p, key)) return;
  const TrainingDef& t = s.data.trainings[id];
  if (p.coins < t.cost) {
    s.toast(p, MessageId::CoinsInsufficient, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  p.coins -= t.cost;
  p.trainings.push_back(key);
  s.sfx(p, SoundId::Book);
  s.toast(p, MessageId::TrainingLearned, {MsgArg::ref(MsgArg::Type::Training, id.value)}, ToastKind::Item);
  bookFirst(s, p, "treino-" + key, BookCat::Historia, BookDomain::Trabalho, "first.treino",
            {MsgArg::ref(MsgArg::Type::Training, id.value)});
}

std::optional<MessageId> craftCheck(const Sim& s, const Player& p, const RecipeDef& r, bool useStorage) {
  if (r.requiredTraining && !hasTraining(p, s.data.trainings.key(r.requiredTraining))) return MessageId::CraftRequires;
  if (!haveAll(p, r, useStorage)) return MessageId::CraftMissing;
  return std::nullopt;
}

bool craft(Sim& s, Player& p, RecipeId id, bool useStorage) {
  const RecipeDef& r = s.data.recipes[id];
  if (craftCheck(s, p, r, useStorage)) return false;
  consume(p, r, useStorage);
  if (s.data.items[r.out].isGear()) {
    addTo(s.data, s.uids, p.inv, r.out, r.quantity, 100.0);
  } else {
    const int got = receive(s, p, r.out, r.quantity);
    if (got < r.quantity) addTo(s.data, s.uids, p.storage, r.out, r.quantity - got);
  }
  s.sfx(p, SoundId::Craft);
  stat(p, "crafted");
  stat(p, "crafted_" + s.data.items.key(r.out), r.quantity);
  TallyData counts;
  for (const auto& [k, v] : p.stats)
    if (k.rfind("crafted_", 0) == 0) counts.emplace_back(k.substr(8), v);
  bookTally(s, p, "oficio", BookCat::Feitos, BookDomain::Trabalho, "tally.oficio", 1, counts);
  return true;
}

RepairCost repairCost(const Gear& g) {
  const rules::RepairCost c = rules::repairCost(g.cond);
  return {c.coins, c.lingote};
}

void repair(Sim& s, Player& p, GearSlot slot) {
  std::optional<Gear>& g = slot == GearSlot::Weapon ? p.equip.weapon : p.equip.armor;
  if (!g || g->cond >= 100) return;
  const RepairCost c = repairCost(*g);
  const ItemId lingote = item(s, "lingote");
  if (p.coins < c.coins) {
    s.toast(p, MessageId::RepairNoCoins, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (c.lingote && count(p.inv, lingote) + count(p.storage, lingote) < 1) {
    s.toast(p, MessageId::RepairNeedsIngot, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  p.coins -= c.coins;
  if (c.lingote && !removeFrom(p.inv, lingote, 1)) removeFrom(p.storage, lingote, 1);
  g->cond = 100;
  // a demo só devolve a arma à mão ao reequipar ou reaparecer; aqui ela volta já no reparo
  if (slot == GearSlot::Weapon) p.weaponShown = true;
  s.sfx(p, SoundId::Craft);
  s.toast(p, MessageId::RepairDone, {itemArg(g->id)}, ToastKind::Item);
  stat(p, "repairs");
  bookTally(s, p, "reparos", BookCat::Feitos, BookDomain::Trabalho, "tally.reparos");
}

}  // namespace rpg::sim
