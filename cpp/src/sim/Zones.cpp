// Matriz de derrota por zona, cargas no chão e retorno ao abrigo (game/zones.js).
#include <algorithm>
#include <cmath>

#include "core/Math.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;

ZoneKind zoneOf(const Sim& s, MapKind map, double x, double z) { return s.statics.zoneAt(map, x, z); }
PlaceId placeOf(const Sim& s, MapKind map, double x, double z) { return s.statics.placeAt(map, x, z); }

LootBag& createLootBag(Sim& s, MapKind map, InstanceId inst, double x, double z, std::vector<InvEntry> items,
                       LootLabel label, const LootBagOpts& o) {
  auto up = std::make_unique<LootBag>();
  LootBag& b = *up;
  b.id = s.newId();
  b.map = map;
  b.instance = inst;
  b.x = x;
  b.z = z;
  b.items = std::move(items);
  b.label = label;
  b.own = o.own;
  b.owner = o.owner;
  b.expires = s.time + (o.ttl != 0 ? o.ttl : 300);
  b.place = placeOf(s, map, x, z);
  b.zone = zoneOf(s, map, x, z);
  s.lootBags.push_back(std::move(up));
  return b;
}

void removeLootBag(Sim& s, EntityId id) {
  for (auto it = s.lootBags.begin(); it != s.lootBags.end(); ++it) {
    if ((*it)->id == id) {
      s.lootBags.erase(it);
      return;
    }
  }
}

void updateLootBags(Sim& s) {
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(s.lootBags.size()) - 1; i >= 0; --i) {
    const LootBag& b = *s.lootBags[static_cast<std::size_t>(i)];
    if (b.items.empty() || s.time > b.expires) s.lootBags.erase(s.lootBags.begin() + i);
  }
}

namespace {

protocol::DeathLine line(const InvEntry& e) {
  return {e.id.value, e.amount()};
}

}  // namespace

protocol::DeathReport handleDeath(Sim& s, Player& p, const DamageSource& src) {
  const ZoneKind zone = zoneOf(s, p.map, p.pos.x, p.pos.z);
  const PlaceId place = placeOf(s, p.map, p.pos.x, p.pos.z);
  protocol::DeathReport report;
  report.zone = static_cast<std::uint8_t>(zone);
  report.place = static_cast<std::uint8_t>(place);
  report.cause = src.enemyType ? MsgArg::ref(MsgArg::Type::Enemy, src.enemyType.value) : MsgArg::message(src.cause);
  report.coinsKept = static_cast<std::int32_t>(p.coins);
  const std::vector<MsgArg> kept = {MsgArg::message(MessageId::KeptKnowledge), MsgArg::message(MessageId::KeptBook),
                                    MsgArg::message(MessageId::KeptCoins), MsgArg::message(MessageId::KeptStorage)};
  const ItemId montaria = item(s, "montaria");
  const auto wearEquipped = [&] {
    for (Gear* g : equippedList(p))
      if (g->id != montaria) g->cond = std::max(0.0, g->cond - 15);
  };
  std::optional<EntityId> bag;

  if (zone == ZoneKind::Protegida) {
    wearEquipped();
    report.kept = kept;
    report.kept.push_back(MsgArg::message(MessageId::KeptCargo));
    report.kept.push_back(MsgArg::message(MessageId::KeptEquipWorn));
  } else if (zone == ZoneKind::Fronteira) {
    std::vector<InvEntry> items = std::move(p.inv);
    p.inv.clear();
    items.insert(items.end(), p.saddle.begin(), p.saddle.end());
    p.saddle.clear();
    wearEquipped();
    for (const InvEntry& e : items) report.lost.push_back(line(e));
    if (!items.empty()) {
      LootBagOpts o;
      o.own = true;
      o.owner = p.id;
      o.ttl = 300;
      bag = createLootBag(s, p.map, p.instance, p.pos.x, p.pos.z, items, LootLabel::YourCargo, o).id;
    }
    report.kept = kept;
    report.kept.push_back(MsgArg::message(MessageId::KeptEquipWorn));
    if (p.equip.mount) report.kept.push_back(MsgArg::message(MessageId::KeptMountItem));
  } else {
    // Full Loot e Turbulenta: tudo o que é levado entra na regra; uma parte é destruída
    std::vector<InvEntry> items = std::move(p.inv);
    p.inv.clear();
    items.insert(items.end(), p.saddle.begin(), p.saddle.end());
    p.saddle.clear();
    for (std::optional<Gear>* g : {&p.equip.weapon, &p.equip.armor, &p.equip.bag, &p.equip.mount}) {
      if (*g) {
        items.push_back(**g);
        g->reset();
      }
    }
    std::vector<InvEntry> survived;
    for (const InvEntry& e : items) {
      if (e.uid) {
        if (s.rng->next() < 0.3) report.destroyed.push_back(line(e));
        else survived.push_back(e);
      } else {
        const int d = static_cast<int>(std::floor(e.qty * 0.3));
        if (d > 0) report.destroyed.push_back({e.id.value, d});
        if (e.qty - d > 0) {
          InvEntry left;
          left.id = e.id;
          left.qty = e.qty - d;
          survived.push_back(left);
        }
      }
    }
    for (const InvEntry& e : survived) report.lost.push_back(line(e));
    if (!survived.empty()) {
      LootBagOpts o;
      o.own = true;
      o.owner = p.id;
      o.ttl = zone == ZoneKind::Turbulenta ? 9999 : 300;
      bag = createLootBag(s, p.map, p.instance, p.pos.x, p.pos.z, survived, LootLabel::YourCargo, o).id;
    }
    report.kept = kept;
  }
  report.bag = bag.has_value();
  report.hasWeapon = p.equip.weapon.has_value();
  if (p.mount.present) despawnMount(s, p);

  // Livro: a derrota também é parte da trajetória
  const double n = stat(p, "deaths");
  bookFirst(s, p, "primeira-derrota", BookCat::Feitos, BookDomain::Risco, "first.primeira-derrota",
            {MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(place)),
             MsgArg::ref(MsgArg::Type::Zone, static_cast<std::uint16_t>(zone))});
  int lostQty = 0, destroyedQty = 0;
  for (const auto& l : report.lost) lostQty += l.qty;
  for (const auto& l : report.destroyed) destroyedQty += l.qty;
  if (zone != ZoneKind::Turbulenta) {
    bookLog(s, p, BookCat::Historia, BookDomain::Risco, "log.derrota",
            {MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(place)), report.cause,
             MsgArg::ref(MsgArg::Type::Zone, static_cast<std::uint16_t>(zone)),
             MsgArg::integer(static_cast<long long>(report.lost.size())), MsgArg::integer(lostQty),
             MsgArg::integer(static_cast<long long>(report.destroyed.size())), MsgArg::integer(destroyedQty)},
            "morte" + std::to_string(static_cast<long long>(n)));
  }
  return report;
}

void respawn(Sim& s, Player& p) {
  const XZ sh = s.layout.shelter;
  p.map = MapKind::Region;
  p.instance = kRegionInstance;
  p.pos = {sh.x, s.mapOf(MapKind::Region).groundHeight(sh.x, sh.z), sh.z};
  p.maxHp = 100 + (p.equip.armor ? s.data.items[p.equip.armor->id].armorHp : 0);
  p.hp = p.maxHp;
  p.vigor = p.maxVigor;
  p.state = PlayerState::Free;
  p.stateT = 0;
  p.mounted = false;
  p.invuln = 1.5;
  p.lastCombat = -99;
  p.deathPanel = false;
  p.weaponShown = p.equip.weapon && p.equip.weapon->cond > 0;  // zones.js respawn: setWeapon(cond > 0 ? … : null)
  s.toPlayer(p, protocol::EvRespawned{});
}

}  // namespace rpg::sim
