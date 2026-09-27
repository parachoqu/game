#include "sim/World.h"

#include <algorithm>
#include <cmath>

#include "core/Assert.h"
#include "core/JsMath.h"
#include "core/Math.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

bool nearService(const Sim& s, const Player& p, std::string_view kind, std::string_view data = {}) {
  if (p.map != MapKind::Region || p.instance != kRegionInstance) return false;
  for (const LayoutInteractable& it : s.layout.interactables) {
    if (it.kind != kind || (!data.empty() && it.data != data)) continue;
    if (jsHypot(p.pos.x - it.x, p.pos.z - it.z) <= it.r + 2.5) return true;
  }
  return false;
}

void tooFar(Sim& s, Player& p) {
  s.toast(p, MessageId::RequestTooFar, {}, ToastKind::Warn);
  s.sfx(p, SoundId::Deny);
}

// panels.js `moveOne`
void moveOne(Sim& s, ItemList& from, ItemList& to, std::size_t i, int n) {
  if (i >= from.size()) return;
  InvEntry e = from[i];
  if (e.uid) {
    from.erase(from.begin() + static_cast<std::ptrdiff_t>(i));
    to.push_back(e);
    return;
  }
  const int q = std::min(n, from[i].qty);
  from[i].qty -= q;
  if (from[i].qty <= 0) from.erase(from.begin() + static_cast<std::ptrdiff_t>(i));
  addTo(s.data, s.uids, to, e.id, q);
}

// panels.js `takeOne`
void takeOne(Sim& s, Player& p, std::size_t i, int n) {
  if (i >= p.storage.size()) return;
  InvEntry& e = p.storage[i];
  if (e.uid) {
    const InvEntry g = e;
    p.storage.erase(p.storage.begin() + static_cast<std::ptrdiff_t>(i));
    p.inv.push_back(g);
    return;
  }
  const int q = std::min(n, e.qty);
  const double w = s.data.items[e.id].weight;
  const double limit = capacity(s, p) * s.data.balance.inventory.overLimit;
  int got = 0;
  for (int k = 0; k < q; ++k) {
    if (weightOf(s.data, p.inv) + w > limit) break;
    addTo(s.data, s.uids, p.inv, e.id, 1);
    got++;
  }
  if (!got) s.toast(p, MessageId::StorageNoCapacity, {}, ToastKind::Warn);
  InvEntry& again = p.storage[i];  // addTo não mexe no armazém: o índice continua válido
  again.qty -= got;
  if (again.qty <= 0) p.storage.erase(p.storage.begin() + static_cast<std::ptrdiff_t>(i));
}

// panels.js `takeFromBag`
void takeFromBag(Sim& s, Player& p, LootBag& bag, std::size_t i) {
  if (i >= bag.items.size()) return;
  InvEntry& e = bag.items[i];
  if (e.uid) {
    if (weightOf(s.data, p.inv) + s.data.items[e.id].weight > capacity(s, p) * s.data.balance.inventory.overLimit) {
      s.toast(p, MessageId::LootNoCapacity, {}, ToastKind::Warn);
      return;
    }
    const InvEntry g = e;
    bag.items.erase(bag.items.begin() + static_cast<std::ptrdiff_t>(i));
    p.inv.push_back(g);
  } else {
    const int got = receive(s, p, e.id, e.qty);
    e.qty -= got;
    if (e.qty <= 0) bag.items.erase(bag.items.begin() + static_cast<std::ptrdiff_t>(i));
    if (!got) {
      s.toast(p, MessageId::LootNoCapacity, {}, ToastKind::Warn);
      return;
    }
  }
  s.sfx(p, SoundId::Pickup);
  if (bag.own && bag.owner == p.id) {
    stat(p, "recovered");
    bookFirst(s, p, "recuperou-carga", BookCat::Feitos, BookDomain::Risco, "first.recuperou-carga",
              {MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(bag.place))});
  }
  if (bag.items.empty()) removeLootBag(s, bag.id);
}

// panels.js `equipFromInv`
void equipFromInv(Sim& s, Player& p, std::size_t i) {
  if (i >= p.inv.size()) return;
  const InvEntry e = p.inv[i];
  const ItemDef& it = s.data.items[e.id];
  if (inCombat(s, p)) {
    s.toast(p, MessageId::GearInCombat, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (it.requiredTraining && !hasTraining(p, s.data.trainings.key(it.requiredTraining))) {
    s.toast(p, MessageId::GearRequiresTraining, {MsgArg::ref(MsgArg::Type::Training, it.requiredTraining.value)}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  std::optional<Gear>* slot = it.kind == ItemKind::Weapon ? &p.equip.weapon
                              : it.kind == ItemKind::Armor ? &p.equip.armor
                              : it.kind == ItemKind::Bag   ? &p.equip.bag
                                                           : nullptr;
  if (!slot) return;
  p.inv.erase(p.inv.begin() + static_cast<std::ptrdiff_t>(i));
  if (*slot) p.inv.push_back(**slot);
  *slot = e;
  if (it.kind == ItemKind::Weapon) p.warnedBroken = false;
  s.sfx(p, SoundId::Ui);
}

protocol::ItemEntry itemEntry(const InvEntry& e) { return {e.id.value, e.qty, e.uid, e.cond}; }

std::vector<protocol::ItemEntry> itemEntries(const ItemList& list) {
  std::vector<protocol::ItemEntry> out;
  out.reserve(list.size());
  for (const InvEntry& e : list) out.push_back(itemEntry(e));
  return out;
}

std::optional<protocol::ItemEntry> gearEntry(const std::optional<Gear>& g) {
  if (!g) return std::nullopt;
  return itemEntry(*g);
}

}  // namespace

// ---------------------------------------------------------------- construção
World::World(const GameData& data, const StaticWorld& statics, WorldConfig config)
    : World(data, statics, std::make_unique<Pcg32Random>(config.seed), config.options, config.devCommands) {}

World::World(const GameData& data, const StaticWorld& statics, std::unique_ptr<IRandom> rng, SimOptions options,
             bool devCommands)
    : s_(std::make_unique<Sim>(data, statics, statics.layout(), std::move(rng), options)), devCommands_(devCommands) {
  populate();
}

World::~World() = default;

// world.js `buildWorld` (NPCs, viajantes, grupos) e o que main.js `startGame` põe no mundo
// (acampamento, guarda do canteiro), na mesma ordem: os sorteios de criação saem na mesma sequência.
void World::populate() {
  Sim& s = *s_;
  initNpcs(s);
  if (!s.opts.populate) return;
  defineGroups(s);
  initCamp(s);
  const LayoutPost& g = s.layout.canteiroGuard;
  SpawnOpts o;
  o.post = Enemy::Post{g.x, g.z, g.yaw};
  Enemy& guard = spawnEnemy(s, enemyType(s, "guarda"), g.x, g.z, o);
  guard.guardPost = true;
}

// ---------------------------------------------------------------- jogadores
EntityId World::addPlayer(const PlayerProfile& profile, std::uint32_t session, bool newCharacter) {
  Player& p = createPlayer(*s_, profile, session);
  inputs_.push_back({p.id, Input{}});
  if (newCharacter)
    bookFirst(*s_, p, "chegada", BookCat::Historia, BookDomain::None, "first.chegada",
              {MsgArg::str(p.name), MsgArg::str(p.start)});
  return p.id;
}

void World::removePlayer(EntityId id) {
  sim::removePlayer(*s_, id);
  std::erase_if(inputs_, [&](const auto& e) { return e.first == id; });
}

World::Input& World::inputOf(EntityId id) {
  for (auto& [pid, in] : inputs_)
    if (pid == id) return in;
  inputs_.push_back({id, Input{}});
  return inputs_.back().second;
}

const World::Input* World::findInput(EntityId id) const {
  for (const auto& [pid, in] : inputs_)
    if (pid == id) return &in;
  return nullptr;
}

void World::setCommand(EntityId player, const protocol::PlayerCommand& cmd) {
  if (!s_->player(player)) return;
  Input& in = inputOf(player);
  if (in.first) {
    // o primeiro comando só estabelece os contadores (nenhum toque antigo vale)
    in.seen = cmd.presses;
    in.first = false;
  }
  in.cmd = cmd;
}

// ---------------------------------------------------------------- pedidos
void World::handleRequest(EntityId player, const protocol::Request& req) {
  Sim& s = *s_;
  Player* pp = s.player(player);
  if (!pp) return;
  Player& p = *pp;
  const bool busy = p.state == PlayerState::Down || p.state == PlayerState::Dead;
  std::visit(
      [&](const auto& r) {
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, protocol::ReqClickAttack>) {
          if (p.aiming || p.uiOpen || p.cinematic || busy) return;
          const Enemy* e = s.enemy(r.enemy);
          if (!e || !e->alive || e->faction == Faction::Guard || e->map != p.map || e->instance != p.instance) return;
          if (jsHypot(e->pos.x - p.pos.x, e->pos.z - p.pos.z) > 60) return;
          ClickCommand c;
          c.type = ClickCommand::Type::Attack;
          c.enemy = e->id;
          p.cmd = c;
        } else if constexpr (std::is_same_v<T, protocol::ReqClickUse>) {
          if (p.aiming || p.uiOpen || p.cinematic || busy) return;
          if (r.kind > static_cast<std::uint8_t>(InteractKind::Exit)) return;
          const InteractRef ref{static_cast<InteractKind>(r.kind), r.id};
          const std::optional<Target> t = resolveTarget(s, p, ref);
          if (!t || !targetValid(s, p, ref) || jsHypot(t->x - p.pos.x, t->z - p.pos.z) > 45) return;
          ClickCommand c;
          c.type = ClickCommand::Type::Use;
          c.target = ref;
          c.best = std::numeric_limits<double>::infinity();
          c.since = s.time;
          p.cmd = c;
        } else if constexpr (std::is_same_v<T, protocol::ReqUsePotion>) {
          if (!p.uiOpen) drinkPotion(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqMountAction>) {
          if (!p.uiOpen && !busy) mountAction(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqBuy>) {
          const MarketId mk{r.market};
          const ItemId id{r.item};
          if (!s.data.markets.contains(mk) || !s.data.items.contains(id) || r.qty < 1) return;
          if (!nearService(s, p, "market", s.data.markets.key(mk))) return tooFar(s, p);
          buy(s, p, mk, id, std::min(r.qty, 99));
        } else if constexpr (std::is_same_v<T, protocol::ReqSell>) {
          const MarketId mk{r.market};
          const ItemId id{r.item};
          if (!s.data.markets.contains(mk) || !s.data.items.contains(id)) return;
          if (!nearService(s, p, "market", s.data.markets.key(mk))) return tooFar(s, p);
          sell(s, p, mk, id, r.all ? count(p.inv, id) : std::max(0, std::min(r.qty, 999)));
        } else if constexpr (std::is_same_v<T, protocol::ReqCraft>) {
          const RecipeId id{r.recipe};
          if (!s.data.recipes.contains(id)) return;
          if (!nearService(s, p, "forge")) return tooFar(s, p);
          craft(s, p, id, r.useStorage);
        } else if constexpr (std::is_same_v<T, protocol::ReqRepair>) {
          if (!nearService(s, p, "forge")) return tooFar(s, p);
          repair(s, p, r.slot == 0 ? GearSlot::Weapon : GearSlot::Armor);
        } else if constexpr (std::is_same_v<T, protocol::ReqLearn>) {
          const TrainingId id{r.training};
          if (!s.data.trainings.contains(id)) return;
          if (!nearService(s, p, "trainer")) return tooFar(s, p);
          learn(s, p, id);
        } else if constexpr (std::is_same_v<T, protocol::ReqRestartKit>) {
          if (!nearService(s, p, "trainer")) return tooFar(s, p);
          const bool noWeapon = !p.equip.weapon && std::none_of(p.inv.begin(), p.inv.end(), [&](const InvEntry& e) {
            return s.data.items[e.id].kind == ItemKind::Weapon;
          });
          if (noWeapon) restartKit(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqStore>) {
          if (!nearService(s, p, "storage")) return tooFar(s, p);
          moveOne(s, p.inv, p.storage, r.index, r.all ? std::numeric_limits<int>::max() : 1);
        } else if constexpr (std::is_same_v<T, protocol::ReqStoreMaterials>) {
          if (!nearService(s, p, "storage")) return tooFar(s, p);
          const ItemId pocao = item(s, "pocao");
          for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(p.inv.size()) - 1; i >= 0; --i) {
            const InvEntry& e = p.inv[static_cast<std::size_t>(i)];
            if (!e.uid && e.id != pocao) moveOne(s, p.inv, p.storage, static_cast<std::size_t>(i), std::numeric_limits<int>::max());
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqWithdraw>) {
          if (!nearService(s, p, "storage")) return tooFar(s, p);
          takeOne(s, p, r.index, r.all ? std::numeric_limits<int>::max() : 1);
        } else if constexpr (std::is_same_v<T, protocol::ReqBuyMount>) {
          if (!nearService(s, p, "stable")) return tooFar(s, p);
          buyMount(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqStableBagsToStorage>) {
          if (!nearService(s, p, "stable")) return tooFar(s, p);
          for (const InvEntry& e : p.stableBags) addEntry(s.data, s.uids, p.storage, e);
          p.stableBags.clear();
          s.toast(p, MessageId::StableBagsStored, {}, ToastKind::Item);
        } else if constexpr (std::is_same_v<T, protocol::ReqDeliver>) {
          const ItemId id{r.item};
          if (!s.data.items.contains(id)) return;
          const std::string& k = s.data.items.key(id);
          if (k != "ferramentas" && k != "lingote") return;
          if (!nearService(s, p, "canteiro")) return tooFar(s, p);
          deliver(s, p, id);
        } else if constexpr (std::is_same_v<T, protocol::ReqGiveInstrument>) {
          if (!nearService(s, p, "astronomer")) return tooFar(s, p);
          if (s.skyState.instrumentGiven) return;
          if (removeFrom(p.inv, item(s, "instrumento"), 1)) giveInstrument(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqTakeLoot>) {
          LootBag* bag = s.lootBag(r.bag);
          if (!bag || bag->map != p.map || bag->instance != p.instance || busy) return;
          if (jsHypot(bag->x - p.pos.x, bag->z - p.pos.z) > 4.5) return tooFar(s, p);
          if (r.index < 0) {
            for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(bag->items.size()) - 1; i >= 0; --i) {
              bag = s.lootBag(r.bag);  // o saco some quando esvazia
              if (!bag) break;
              takeFromBag(s, p, *bag, static_cast<std::size_t>(i));
            }
          } else {
            takeFromBag(s, p, *bag, static_cast<std::size_t>(r.index));
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqEnterPortal>) {
          if (!s.turb.portal || p.turb.inside) return;
          if (jsHypot(s.turb.portal->x - p.pos.x, s.turb.portal->z - p.pos.z) > 6.5) return tooFar(s, p);
          if (canEnter(p)) return;
          enterTurbulent(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqRespawn>) {
          if (p.state == PlayerState::Dead) respawn(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqEquip>) {
          equipFromInv(s, p, r.index);
        } else if constexpr (std::is_same_v<T, protocol::ReqUnequip>) {
          if (inCombat(s, p)) {
            s.toast(p, MessageId::GearInCombat, {}, ToastKind::Warn);
            s.sfx(p, SoundId::Deny);
            return;
          }
          std::optional<Gear>& g = r.slot == 0 ? p.equip.weapon : r.slot == 1 ? p.equip.armor : p.equip.bag;
          if (!g) return;
          p.inv.push_back(*g);
          g.reset();
        } else if constexpr (std::is_same_v<T, protocol::ReqDrink>) {
          if (p.hp < p.maxHp && removeFrom(p.inv, item(s, "pocao"), 1)) {
            p.hp = std::min(p.maxHp, p.hp + 45);
            s.sfx(p, SoundId::Pickup);
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqToSaddle>) {
          if (!saddleReachable(p) || r.index >= p.inv.size()) return;
          const InvEntry e = p.inv[r.index];
          if (weightOf(s.data, p.saddle) + s.data.items[e.id].weight > saddleCap(s, p)) {
            s.toast(p, MessageId::SaddleFull, {}, ToastKind::Warn);
            return;
          }
          if (e.uid) {
            p.inv.erase(p.inv.begin() + r.index);
            p.saddle.push_back(e);
          } else {
            removeFrom(p.inv, e.id, 1);
            addTo(s.data, s.uids, p.saddle, e.id, 1);
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqFromSaddle>) {
          if (!saddleReachable(p) || r.index >= p.saddle.size()) return;
          const InvEntry e = p.saddle[r.index];
          if (weightOf(s.data, p.inv) + s.data.items[e.id].weight > capacity(s, p) * s.data.balance.inventory.overLimit) {
            s.toast(p, MessageId::BagLimit, {}, ToastKind::Warn);
            return;
          }
          if (e.uid) {
            p.saddle.erase(p.saddle.begin() + r.index);
            p.inv.push_back(e);
          } else {
            removeFrom(p.saddle, e.id, 1);
            addTo(s.data, s.uids, p.inv, e.id, 1);
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqDrop>) {
          if (r.index >= p.inv.size() || busy) return;
          const InvEntry e = p.inv[r.index];
          p.inv.erase(p.inv.begin() + r.index);
          LootBagOpts o;
          o.own = true;
          o.owner = p.id;
          o.ttl = 240;
          createLootBag(s, p.map, p.instance, p.pos.x + js::sin(p.yaw) * 1.2, p.pos.z + js::cos(p.yaw) * 1.2, {e},
                        LootLabel::Dropped, o);
          s.toast(p, MessageId::ItemDropped, {MsgArg::ref(MsgArg::Type::Item, e.id.value)}, ToastKind::Info);
        } else if constexpr (std::is_same_v<T, protocol::ReqEndObserve>) {
          endObserve(s, p);
        } else if constexpr (std::is_same_v<T, protocol::ReqBookSetPublic>) {
          for (BookEntry& e : p.book.entries()) {
            if (e.id == r.entry && !e.personal) {
              e.isPublic = r.isPublic;
              p.book.touch();
              s.toPlayer(p, protocol::EvBookEntry{e, false});
            }
          }
        } else if constexpr (std::is_same_v<T, protocol::ReqBookNote>) {
          std::string text = r.text.substr(0, 600);
          if (!text.empty()) s.bookEvent(p, p.book.note(s.time, s.day(), std::move(text)));
        } else if constexpr (std::is_same_v<T, protocol::ReqShowTitle>) {
          if (!r.title) {
            p.book.setShownTitle(std::nullopt);
          } else {
            const auto& titles = p.book.titles();
            if (std::any_of(titles.begin(), titles.end(), [&](const BookTitle& t) { return t.id == *r.title; }))
              p.book.setShownTitle(p.book.shownTitle() == r.title ? std::nullopt : r.title);
          }
          p.book.touch();
        } else if constexpr (std::is_same_v<T, protocol::ReqPause>) {
          paused_ = r.paused;
        } else if constexpr (std::is_same_v<T, protocol::ReqDev>) {
          if (devCommands_) handleDev(p, r);
        }
      },
      req);
}

void World::handleDev(Player& p, const protocol::ReqDev& r) {
  Sim& s = *s_;
  using protocol::DevCommand;
  switch (r.cmd) {
    case DevCommand::Teleport: {
      const MapKind map = s.statics.mapAtDemoX(r.a);
      p.map = map;
      if (map == MapKind::Region) p.instance = kRegionInstance;
      p.pos = {r.a, s.mapOf(map).groundHeight(r.a, r.b), r.b};
      break;
    }
    case DevCommand::Give:
      if (s.data.items.contains(ItemId{r.item})) receive(s, p, ItemId{r.item}, std::max(1, static_cast<int>(r.a)));
      break;
    case DevCommand::Coins: p.coins += r.a; break;
    case DevCommand::Hurt: {
      DamageSource src;
      src.x = p.pos.x + 1;
      src.z = p.pos.z;
      src.cause = MessageId::CauseTest;
      hurtPlayer(s, p, r.a != 0 ? r.a : 999, src);
      break;
    }
    case DevCommand::Portal: forcePortalNear(s, p); break;
    case DevCommand::KillNear: {
      const double radius = r.a != 0 ? r.a : 30;
      const int max = r.b != 0 ? static_cast<int>(r.b) : 99;
      int n = 0;
      std::vector<EntityId> ids;
      for (const auto& e : s.enemies) ids.push_back(e->id);
      for (const EntityId id : ids) {
        if (n >= max) break;
        Enemy* e = s.enemy(id);
        if (e && e->alive && e->faction != Faction::Guard && e->map == p.map && e->instance == p.instance &&
            jsHypot(e->pos.x - p.pos.x, e->pos.z - p.pos.z) < radius) {
          HurtOpts o;
          o.attacker = p.id;
          hurtEnemy(s, *e, 9999, o);
          n++;
        }
      }
      break;
    }
    case DevCommand::Contribute: contribute(s, &p, "ferramentas", r.a != 0 ? r.a : 10); break;
    case DevCommand::Night: s.time += secondsUntilHour(s.time, 22, s.data.balance.clock); break;
    case DevCommand::Vanish: s.skyState.next = s.time; break;
  }
}

// ---------------------------------------------------------------- passo
void World::step(Seconds dt) {
  RPG_ASSERT(dt > 0.0, "World::step: dt precisa ser positivo");
  Sim& s = *s_;
  if (paused_) return;
  using protocol::Press;

  // entradas do passo: toques novos desde o último comando visto
  std::vector<std::pair<Player*, PlayerInput>> frame;
  for (auto& pp : s.players) {
    Player& p = *pp;
    Input& in = inputOf(p.id);
    PlayerInput pi;
    pi.cmd = in.cmd;
    for (std::size_t k = 0; k < pi.pressed.size(); ++k) {
      pi.pressed[k] = in.cmd.presses[k] != in.seen[k];
      in.seen[k] = in.cmd.presses[k];
    }
    p.uiOpen = in.cmd.has(protocol::kHeldUiOpen);
    frame.emplace_back(&p, pi);
  }

  // main.js `handleKeys`: R chama/monta antes da simulação do passo
  for (auto& [p, in] : frame) {
    if (p->cinematic || p->state == PlayerState::Dead) continue;
    if (in.hit(Press::Mount) && !p->uiOpen) mountAction(s, *p);
  }

  s.time += dt;
  ++s.tick;

  // pointer.js: mirando, o comando de clique é descartado a cada quadro
  for (auto& [p, in] : frame) {
    const bool blocked = p->uiOpen || p->cinematic || p->state == PlayerState::Down || p->state == PlayerState::Dead;
    if (!blocked && p->aiming) p->cmd.reset();
  }
  for (auto& [p, in] : frame) updatePlayer(s, *p, in, dt);
  if (s.opts.stepMount)
    for (auto& [p, in] : frame) updateMount(s, *p);
  if (s.opts.stepEnemies) updateEnemies(s, dt);
  if (s.opts.stepGroups) updateGroups(s);
  if (s.opts.stepProjectiles) updateProjectiles(s, dt);
  if (s.opts.stepCamp) updateCamp(s, dt);
  if (s.opts.stepEvent) updateEvent(s, dt);
  if (s.opts.stepStars) updateStars(s, dt, s.nightNow);
  if (s.opts.stepTurbulent) updateTurbulent(s, dt);
  if (s.opts.stepMarkets) updateMarkets(s, dt);
  if (s.opts.stepLootBags) updateLootBags(s);
  if (s.opts.stepNpcs) updateNpcs(s, dt);
  if (s.opts.stepInteract) {
    regrowNodes(s);
    for (auto& [p, in] : frame) {
      if (!s.player(p->id)) continue;
      updateInteract(s, *p, in.hit(Press::Interact));
    }
  }
  if (s.opts.stepDiscovery)
    for (auto& [p, in] : frame) updateDiscovery(s, *p, dt);

  // main.js: a luz do dia é calculada depois do passo e vale para o passo seguinte (G.nightNow)
  s.nightNow = daylightAt(clockAt(s.time, s.data.balance.clock).hour).night;
}

std::vector<OutEvent> World::takeEvents() {
  std::vector<OutEvent> out;
  out.swap(s_->out);
  return out;
}

protocol::EvBookSync World::bookSync(EntityId player) const {
  protocol::EvBookSync sync;
  if (const Player* p = s_->player(player)) {
    sync.entries = p->book.entries();
    sync.titles = p->book.titles();
    sync.shownTitle = p->book.shownTitle();
  }
  return sync;
}

// ---------------------------------------------------------------- snapshot
protocol::Snapshot World::snapshot(EntityId viewerId, double radius) const {
  const Sim& s = *s_;
  protocol::Snapshot snap;
  auto& W = snap.world;
  W.tick = s.tick;
  W.time = s.time;
  W.paused = paused_;
  W.evtProgress = s.evt.progress;
  W.evtDone = s.evt.done;
  W.evtOthersPts = s.evt.othersPts;
  W.evtTotalPts = s.evt.playersPts + s.evt.othersPts;
  W.campState = static_cast<std::uint8_t>(s.camp.state);
  for (const VanishedStar& v : s.skyState.vanished) W.vanished.push_back({v.index, v.constellation, v.time});
  W.instrumentGiven = s.skyState.instrumentGiven;
  if (s.turb.portal) {
    W.portal = true;
    W.portalX = s.turb.portal->x;
    W.portalZ = s.turb.portal->z;
    W.portalExpires = s.turb.portal->expires;
    W.portalPlace = static_cast<std::uint8_t>(s.turb.portal->place);
  }
  for (std::size_t mk = 0; mk < s.demand.size(); ++mk) {
    protocol::MarketState m;
    for (const auto& [id, d] : s.demand[mk]) m.demand[id.value] = d;
    for (std::size_t id = 0; id < s.sell[mk].size(); ++id)
      if (s.sell[mk][id]) m.sellBase[static_cast<std::uint16_t>(id)] = *s.sell[mk][id];
    W.markets.push_back(std::move(m));
  }
  for (const NodeRuntime& n : s.nodes) W.nodes.push_back({static_cast<std::uint8_t>(n.charges), n.regrowAt});

  const Player* viewer = s.player(viewerId);
  if (viewer) {
    const Player& p = *viewer;
    W.evtContrib = p.evt.contrib;
    W.evtPlayerPts = p.evt.pts;
    W.observed = std::set<std::int32_t>(p.observed.begin(), p.observed.end());
    protocol::PrivateState me;
    me.playerId = p.id;
    if (const Input* in = findInput(p.id)) me.lastCommandSeq = in->cmd.seq;
    me.hp = p.hp;
    me.maxHp = p.maxHp;
    me.vigor = p.vigor;
    me.maxVigor = p.maxVigor;
    me.coins = p.coins;
    me.state = static_cast<std::uint8_t>(p.state);
    me.stateT = p.stateT;
    me.invuln = p.invuln;
    me.cdQ = p.cd.q;
    me.cdE = p.cd.e;
    me.cdPot = p.cd.pot;
    if (p.channel) me.channel = protocol::ChannelState{static_cast<std::uint16_t>(p.channel->label), p.channel->t, p.channel->dur};
    me.zone = static_cast<std::uint8_t>(p.zone);
    me.place = static_cast<std::uint8_t>(p.place);
    me.load = static_cast<std::uint8_t>(loadState(s, p));
    me.bagWeight = weightOf(s.data, p.inv);
    me.capacity = capacity(s, p);
    me.saddleWeight = weightOf(s.data, p.saddle);
    me.saddleCapacity = saddleCap(s, p);
    me.saddleReachable = saddleReachable(p);
    me.inv = itemEntries(p.inv);
    me.saddle = itemEntries(p.saddle);
    me.storage = itemEntries(p.storage);
    me.stableBags = itemEntries(p.stableBags);
    me.weapon = gearEntry(p.equip.weapon);
    me.armor = gearEntry(p.equip.armor);
    me.bag = gearEntry(p.equip.bag);
    me.mount = gearEntry(p.equip.mount);
    me.trainings = p.trainings;
    me.metamorphT = p.metamorphT;
    me.shieldHp = p.shieldHp;
    me.shieldT = p.shieldT;
    me.auraVitalT = p.auraVitalT;
    me.mounted = p.mounted;
    me.aiming = p.aiming;
    me.crouch = p.crouch;
    me.inCombat = inCombat(s, p);
    me.cinematic = p.cinematic;
    me.anon = p.anon;
    if (p.air) me.airY = p.air->y;
    me.lastCombat = p.lastCombat;
    if (p.cmd && p.cmd->type == ClickCommand::Type::Attack) me.attackTarget = p.cmd->enemy;
    me.mountPresent = p.mount.present;
    me.mountX = p.mount.pos.x;
    me.mountZ = p.mount.pos.z;
    me.turb.inside = p.turb.inside;
    me.turb.enteredAt = p.turb.enteredAt;
    me.turb.collapseAt = p.turb.collapseAt;
    for (const Shrine& sh : p.turb.shrines) me.turb.shrinesTaken.push_back(sh.taken ? 1 : 0);
    for (const auto& [k, v] : p.stats) me.stats[k] = v;
    me.flags = p.flags;
    me.landT = p.landT;
    me.crouchW = p.crouchW;
    me.aimLock = p.aimLock;
    if (p.air) {
      me.airVy = p.air->vy;
      me.airVx = p.air->vx;
      me.airVz = p.air->vz;
      me.airT = p.air->t;
      me.airDur = p.air->dur;
      me.airBoosted = p.air->boosted;
    }
    snap.self = std::move(me);
  }

  const auto visible = [&](MapKind map, InstanceId inst, double x, double z) {
    if (!viewer) return true;
    return map == viewer->map && inst == viewer->instance && jsHypot(x - viewer->pos.x, z - viewer->pos.z) <= radius;
  };

  for (const auto& pp : s.players) {
    const Player& p = *pp;
    if (p.id != viewerId && !visible(p.map, p.instance, p.pos.x, p.pos.z)) continue;
    protocol::EntityState e;
    e.id = p.id;
    e.kind = protocol::EntityKind::Player;
    e.map = static_cast<std::uint8_t>(p.map);
    e.instance = p.instance;
    e.x = p.pos.x;
    e.y = p.pos.y;
    e.z = p.pos.z;
    e.yaw = p.yaw;
    e.state = static_cast<std::uint8_t>(p.state);
    e.stateT = p.stateT;
    e.speed = p.mounted ? 0 : p.speedNow;
    if (p.equip.weapon) e.weapon = s.data.items[p.equip.weapon->id].family.value;
    if (p.state == PlayerState::Attack && p.basic) {
      e.action = static_cast<std::uint8_t>(p.basic->kind);
      e.actionProgress = std::min(1.0, p.stateT / (p.basic->windup + p.basic->recover));
    } else if (p.state == PlayerState::Skill && p.skill) {
      const anim::SkillPose pose = anim::skillPose(s.data.skills.key(p.skill->skill));
      e.action = static_cast<std::uint8_t>(pose.kind);
      e.actionProgress = anim::skillProgress(pose, p.stateT);
    }
    if (p.state == PlayerState::Dodge) {
      e.dodgeYaw = p.dodgeYaw;
      e.dodgeProgress = std::min(1.0, p.stateT / 0.42);
    }
    if (p.hitAt && s.time - *p.hitAt < 3) {
      e.hitSide = static_cast<std::uint8_t>(p.hitDir);
      e.hitAge = s.time - *p.hitAt;
    }
    e.aimPitch = p.aimPitch;
    if (p.air) {
      e.airPhase = std::min(1.0, p.air->t / p.air->dur);
      if (p.air->boosted) e.flags |= protocol::kFlagAirBoosted;
    }
    e.moveX = p.moveX;
    e.moveZ = p.moveZ;
    if (p.moving) e.flags |= protocol::kFlagMoving;
    if (p.crouch) e.flags |= protocol::kFlagCrouch;
    if (p.mounted) e.flags |= protocol::kFlagMounted;
    if (p.metamorphT > 0) e.flags |= protocol::kFlagMetamorph;
    if (p.state == PlayerState::Channel && p.channel && p.channel->anim) e.flags |= protocol::kFlagChannelAnim;
    if (p.state == PlayerState::Down || p.state == PlayerState::Dead) e.flags |= protocol::kFlagDown;
    if (p.invuln > 0.3 && p.state == PlayerState::Free && static_cast<long long>(std::floor(s.time * 12)) % 2 == 0)
      e.flags |= protocol::kFlagBlink;
    if (p.anon) e.flags |= protocol::kFlagAnon;
    e.flags |= protocol::kFlagAlive;
    e.hpFrac = p.maxHp > 0 ? p.hp / p.maxHp : 0;
    if (!p.anon || p.id == viewerId) {
      e.name = p.name;
      e.origin = p.origin;
      e.model = p.model;
    }
    snap.entities.push_back(std::move(e));

    if (p.mount.present && visible(MapKind::Region, kRegionInstance, p.mount.pos.x, p.mount.pos.z)) {
      protocol::EntityState m;
      m.id = p.mount.id;
      m.kind = protocol::EntityKind::Mount;
      m.x = p.mount.pos.x;
      m.y = p.mount.y;
      m.z = p.mount.pos.z;
      m.yaw = p.mount.yaw;
      m.speed = p.mounted ? p.speedNow / motor::kMountedSpeed : 0;
      if (p.mounted) m.flags |= protocol::kFlagMounted;
      if (p.id == viewerId) m.flags |= protocol::kFlagOwn;
      snap.entities.push_back(std::move(m));
    }
  }

  for (const auto& ep : s.enemies) {
    const Enemy& en = *ep;
    if (!visible(en.map, en.instance, en.pos.x, en.pos.z)) continue;
    const EnemyDef& cfg = s.data.enemies[en.type];
    protocol::EntityState e;
    e.id = en.id;
    e.kind = protocol::EntityKind::Enemy;
    e.map = static_cast<std::uint8_t>(en.map);
    e.instance = en.instance;
    e.x = en.pos.x;
    e.y = en.pos.y;
    e.z = en.pos.z;
    e.yaw = en.yaw;
    e.type = en.type.value;
    e.weapon = cfg.weapon.valid() ? cfg.weapon.value : 0xFFFF;
    e.state = static_cast<std::uint8_t>(en.state);
    e.stateT = en.t;
    e.speed = en.speedNow;
    e.hpFrac = en.maxHp > 0 ? en.hp / en.maxHp : 0;
    e.hurtT = en.hurtT;
    e.scale = cfg.scale;
    const bool human = en.faction != Faction::Beast;
    if (human) e.flags |= protocol::kFlagHuman;
    if (en.alive) e.flags |= protocol::kFlagAlive;
    if (en.barShown) e.flags |= protocol::kFlagBar;
    if (en.asleep) e.flags |= protocol::kFlagAsleep;
    if (en.faction == Faction::Shadow) e.flags |= protocol::kFlagAnon;
    if (en.state == EnemyState::Stun) e.flags |= protocol::kFlagStun;
    if (!en.alive) {
      e.flags |= protocol::kFlagDown;
      e.deathFall = static_cast<std::uint8_t>(en.deathDir);
      e.deadT = en.t;
    }
    if (en.alive && en.hitAt && s.time - *en.hitAt < 3) {
      e.hitSide = static_cast<std::uint8_t>(en.hitDir);
      e.hitAge = s.time - *en.hitAt;
    }
    // enemies.js `enemyAnimState`
    if (!human) {
      if (en.state == EnemyState::Windup) e.actionProgress = std::min(1.0, en.t / cfg.windup);
      else if (en.state == EnemyState::Recover && en.atk >= 0) e.actionProgress = en.atk;
    } else {
      const bool ranged = cfg.ranged || (cfg.weapon.valid() && s.data.weapons.key(cfg.weapon) == "arco");
      e.action = static_cast<std::uint8_t>(ActionKind::Swing);
      if (en.state == EnemyState::Windup) {
        e.actionProgress = ranged ? std::min(1.0, en.t / cfg.windup) : std::min(0.34, en.t / cfg.windup * 0.34);
        e.action = static_cast<std::uint8_t>(ranged ? ActionKind::Bow : ActionKind::Swing);
      } else if (en.atk >= 0) {
        e.actionProgress = ranged ? -1 : 0.35 + en.atk * 0.65;
      }
    }
    snap.entities.push_back(std::move(e));
  }

  for (const Npc& n : s.npcs) {
    if (!visible(MapKind::Region, kRegionInstance, n.pos.x, n.pos.z)) continue;
    protocol::EntityState e;
    e.id = n.id;
    e.kind = protocol::EntityKind::Npc;
    e.x = n.pos.x;
    e.y = n.pos.y;
    e.z = n.pos.z;
    e.yaw = n.yaw;
    e.type = static_cast<std::uint16_t>(n.layoutIndex);
    e.speed = n.speedNow;
    e.flags |= protocol::kFlagAlive | protocol::kFlagHuman;
    if (n.work && js::sin(s.time * 0.7 + n.pos.x) > 0.2) e.flags |= protocol::kFlagChannelAnim | protocol::kFlagWork;
    snap.entities.push_back(std::move(e));
  }

  for (const auto& pp : s.projectiles) {
    const Projectile& p = *pp;
    if (!visible(p.map, p.instance, p.x, p.z)) continue;
    protocol::EntityState e;
    e.id = p.id;
    e.kind = protocol::EntityKind::Projectile;
    e.map = static_cast<std::uint8_t>(p.map);
    e.instance = p.instance;
    e.x = p.x;
    e.y = p.y;
    e.z = p.z;
    e.dx = p.dx;
    e.dy = p.dy;
    e.dz = p.dz;
    e.type = static_cast<std::uint16_t>(static_cast<unsigned>(p.kind) | (static_cast<unsigned>(p.school) << 4) |
                                        (static_cast<unsigned>(p.owner) << 8));
    e.speed = p.speed;
    if (p.ballistic) e.flags |= protocol::kFlagBallistic;
    snap.entities.push_back(std::move(e));
  }

  for (const auto& bp : s.lootBags) {
    const LootBag& b = *bp;
    if (!visible(b.map, b.instance, b.x, b.z)) continue;
    protocol::EntityState e;
    e.id = b.id;
    e.kind = protocol::EntityKind::LootBag;
    e.map = static_cast<std::uint8_t>(b.map);
    e.instance = b.instance;
    e.x = b.x;
    e.y = s.mapOf(b.map).groundHeight(b.x, b.z);
    e.z = b.z;
    e.type = static_cast<std::uint16_t>(b.label);
    e.stateT = b.expires;
    if (b.own && b.owner == viewerId) e.flags |= protocol::kFlagOwn;
    e.state = static_cast<std::uint8_t>(b.zone);
    e.hitSide = static_cast<std::uint8_t>(b.place);
    snap.entities.push_back(std::move(e));
  }
  return snap;
}

}  // namespace rpg::sim
