// Alvos de interação: serviços, coleta, cargas no chão, viajantes, portal, santuários, saídas e
// montaria (game/interact.js). A mesma lista serve à tecla F (mais próximo dentro do raio) e ao
// clique (pedido ReqClickUse, conferido aqui).
#include <algorithm>
#include <cmath>

#include "core/Math.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::Service;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

InteractKind kindFromLayout(const std::string& k) {
  if (k == "node") return InteractKind::Node;
  if (k == "market") return InteractKind::Market;
  if (k == "forge") return InteractKind::Forge;
  if (k == "trainer") return InteractKind::Trainer;
  if (k == "storage") return InteractKind::Storage;
  if (k == "stable") return InteractKind::Stable;
  if (k == "board") return InteractKind::Board;
  if (k == "canteiro") return InteractKind::Canteiro;
  if (k == "astronomer") return InteractKind::Astronomer;
  if (k == "vigia") return InteractKind::Vigia;
  return InteractKind::Observatory;
}

MessageId gatherVerb(const std::string& kind) {
  if (kind == "minerio") return MessageId::ChannelMine;
  if (kind == "madeira") return MessageId::ChannelChop;
  if (kind == "erva") return MessageId::ChannelHerb;
  return MessageId::ChannelCrystal;
}

bool inRegion(const Player& p) { return p.map == MapKind::Region && p.instance == kRegionInstance; }

void openService(Sim& s, Player& p, Service svc, std::uint32_t data = 0) {
  s.toPlayer(p, protocol::EvServiceOpened{svc, data});
}

void service(Sim& s, Player& p, std::size_t index) {
  const LayoutInteractable& it = s.layout.interactables[index];
  switch (kindFromLayout(it.kind)) {
    case InteractKind::Market: openService(s, p, Service::Market, s.data.markets.find(it.data).value); break;
    case InteractKind::Forge: openService(s, p, Service::Forge); break;
    case InteractKind::Trainer: openService(s, p, Service::Trainer); break;
    case InteractKind::Storage: openService(s, p, Service::Storage); break;
    case InteractKind::Stable: openService(s, p, Service::Stable); break;
    case InteractKind::Board:
      openService(s, p, Service::Board);
      bookFirst(s, p, "disc_board", BookCat::Historia, BookDomain::Exploracao, "first.disc_board");
      break;
    case InteractKind::Canteiro: openService(s, p, Service::Canteiro); break;
    case InteractKind::Astronomer: openService(s, p, Service::Astronomer); break;
    case InteractKind::Vigia: startChannel(p, MessageId::ChannelRecon, 3, ChannelAction::Recon); break;
    case InteractKind::Observatory: observe(s, p, s.nightNow); break;
    default: break;
  }
}

void gather(Sim& s, Player& p, std::size_t nodeIndex) {
  if (inCombat(s, p)) {
    s.toast(p, MessageId::GatherInCombat, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  startChannel(p, gatherVerb(s.layout.nodes[nodeIndex].kind), 1.3, ChannelAction::Gather, static_cast<std::uint32_t>(nodeIndex));
}

void finishGather(Sim& s, Player& p, std::size_t nodeIndex) {
  const LayoutNode& def = s.layout.nodes[nodeIndex];
  NodeRuntime& n = s.nodes[nodeIndex];
  const ItemId id = item(s, def.kind);
  const int q = 1 + def.yieldBonus + (s.rng->next() < 0.35 ? 1 : 0);
  const int got = receive(s, p, id, q);
  if (!got) {
    s.toast(p, MessageId::GatherNoCapacity, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  n.charges--;
  if (n.charges <= 0) n.regrowAt = s.time + 55;
  s.sfx(p, SoundId::Gather);
  s.toast(p, got < q ? MessageId::GatherGotPartial : MessageId::GatherGot,
          {MsgArg::integer(got), MsgArg::ref(MsgArg::Type::Item, id.value)}, ToastKind::Item, 2400);
  stat(p, "gather_" + def.kind, got);
  bookTally(s, p, "coleta-" + def.kind, BookCat::Feitos, BookDomain::Trabalho, "tally.coleta-" + def.kind, got);
}

void finishShrine(Sim& s, Player& p, std::size_t index) {
  if (!p.turb.inside || index >= p.turb.shrines.size()) return;
  Shrine& sh = p.turb.shrines[index];
  if (sh.taken) return;
  const int got = receive(s, p, item(s, "fragmento"), 1);
  if (!got) {
    s.toast(p, MessageId::TurbFragmentNoCapacity, {}, ToastKind::Warn);
    return;
  }
  sh.taken = true;
  s.toast(p, MessageId::TurbFragmentGot, {}, ToastKind::Item);
  s.sfx(p, SoundId::Pickup);
}

}  // namespace

std::vector<Target> targetsNear(Sim& s, const Player& p, double maxDist) {
  std::vector<Target> list;
  const auto near = [&](double x, double z) { return jsHypot(p.pos.x - x, p.pos.z - z) <= maxDist; };
  if (p.turb.inside) {
    for (std::size_t i = 0; i < p.turb.shrines.size(); ++i) {
      const Shrine& sh = p.turb.shrines[i];
      if (!sh.taken && near(sh.x, sh.z)) list.push_back({{InteractKind::Shrine, static_cast<std::uint32_t>(i)}, sh.x, sh.z, 3.2, 0, true});
    }
    for (std::size_t i = 0; i < p.turb.exits.size(); ++i) {
      const TurbExit& e = p.turb.exits[i];
      if (near(e.x, e.z)) list.push_back({{InteractKind::Exit, static_cast<std::uint32_t>(i)}, e.x, e.z, 3.6, 0, true});
    }
    return list;
  }
  if (!inRegion(p)) return list;
  for (std::size_t i = 0; i < s.layout.interactables.size(); ++i) {
    const LayoutInteractable& it = s.layout.interactables[i];
    if (!near(it.x, it.z)) continue;
    const InteractKind kind = kindFromLayout(it.kind);
    const InteractRef ref{kind, static_cast<std::uint32_t>(i)};
    if (kind == InteractKind::Node) {
      const NodeRuntime& n = s.nodes[static_cast<std::size_t>(it.node)];
      list.push_back({ref, it.x, it.z, it.r, 0, n.charges > 0});  // esgotado: aparece, mas não se usa
      continue;
    }
    if (kind == InteractKind::Vigia && p.evt.get("reconhecimento") > 0) continue;
    list.push_back({ref, it.x, it.z, it.r, 0, true});
  }
  for (const auto& bp : s.lootBags) {
    const LootBag& b = *bp;
    if (b.map != p.map || b.instance != p.instance) continue;
    if (!b.items.empty() && near(b.x, b.z)) list.push_back({{InteractKind::LootBag, b.id}, b.x, b.z, 2.6, 0.5, true});
  }
  for (std::size_t i = 0; i < s.npcs.size(); ++i) {
    const Npc& n = s.npcs[i];
    if (n.role == NpcRole::Traveler && near(n.pos.x, n.pos.z))
      list.push_back({{InteractKind::Traveler, static_cast<std::uint32_t>(i)}, n.pos.x, n.pos.z, 3, 0, true});
  }
  if (const auto& portal = s.turb.portal; portal && near(portal->x, portal->z))
    list.push_back({{InteractKind::Portal, portal->id}, portal->x, portal->z, 4.5, 1, true});
  if (p.mount.present && !p.mounted && near(p.mount.pos.x, p.mount.pos.z))
    list.push_back({{InteractKind::Mount, 0}, p.mount.pos.x, p.mount.pos.z, 3.5, 0, true});
  return list;
}

std::optional<Target> nearestInRange(Sim& s, const Player& p) {
  std::optional<Target> best;
  double bd = std::numeric_limits<double>::infinity();
  for (const Target& t : targetsNear(s, p, 8)) {
    const double d = jsHypot(p.pos.x - t.x, p.pos.z - t.z) - t.bias;
    if (d <= t.r && d < bd) {
      bd = d;
      best = t;
    }
  }
  return best;
}

std::optional<Target> resolveTarget(Sim& s, const Player& p, const InteractRef& ref) {
  switch (ref.kind) {
    case InteractKind::Shrine:
      if (!p.turb.inside || ref.id >= p.turb.shrines.size()) return std::nullopt;
      return Target{ref, p.turb.shrines[ref.id].x, p.turb.shrines[ref.id].z, 3.2, 0, true};
    case InteractKind::Exit:
      if (!p.turb.inside || ref.id >= p.turb.exits.size()) return std::nullopt;
      return Target{ref, p.turb.exits[ref.id].x, p.turb.exits[ref.id].z, 3.6, 0, true};
    case InteractKind::LootBag: {
      const LootBag* b = s.lootBag(ref.id);
      if (!b) return std::nullopt;
      return Target{ref, b->x, b->z, 2.6, 0.5, true};
    }
    case InteractKind::Traveler: {
      if (ref.id >= s.npcs.size()) return std::nullopt;
      const Npc& n = s.npcs[ref.id];
      return Target{ref, n.pos.x, n.pos.z, 3, 0, true};
    }
    case InteractKind::Portal:
      if (!s.turb.portal || s.turb.portal->id != ref.id) return std::nullopt;
      return Target{ref, s.turb.portal->x, s.turb.portal->z, 4.5, 1, true};
    case InteractKind::Mount:
      if (!p.mount.present) return std::nullopt;
      return Target{ref, p.mount.pos.x, p.mount.pos.z, 3.5, 0, true};
    default: {
      if (ref.id >= s.layout.interactables.size()) return std::nullopt;
      const LayoutInteractable& it = s.layout.interactables[ref.id];
      if (kindFromLayout(it.kind) != ref.kind) return std::nullopt;
      return Target{ref, it.x, it.z, it.r, 0, true};
    }
  }
}

bool targetValid(Sim& s, const Player& p, const InteractRef& ref) {
  switch (ref.kind) {
    case InteractKind::Shrine: return p.turb.inside && ref.id < p.turb.shrines.size() && !p.turb.shrines[ref.id].taken;
    case InteractKind::Node: {
      if (ref.id >= s.layout.interactables.size()) return false;
      const int node = s.layout.interactables[ref.id].node;
      return node >= 0 && s.nodes[static_cast<std::size_t>(node)].charges > 0;
    }
    case InteractKind::LootBag: {
      const LootBag* b = s.lootBag(ref.id);
      return b && !b->items.empty();
    }
    case InteractKind::Portal: return s.turb.portal && s.turb.portal->id == ref.id;
    case InteractKind::Mount: return p.mount.present && !p.mounted;
    default: return resolveTarget(s, p, ref).has_value();
  }
}

void runTarget(Sim& s, Player& p, const InteractRef& ref) {
  switch (ref.kind) {
    case InteractKind::Node: {
      const int node = s.layout.interactables[ref.id].node;
      if (node >= 0 && s.nodes[static_cast<std::size_t>(node)].charges > 0) gather(s, p, static_cast<std::size_t>(node));
      break;
    }
    case InteractKind::LootBag: openService(s, p, Service::Loot, ref.id); break;
    case InteractKind::Traveler: {
      const Npc& n = s.npcs[ref.id];
      openService(s, p, Service::Traveler, static_cast<std::uint32_t>(n.traveler));
      break;
    }
    case InteractKind::Portal: openService(s, p, Service::Portal, ref.id); break;
    case InteractKind::Mount: mountAction(s, p); break;
    case InteractKind::Shrine: startChannel(p, MessageId::ChannelShrine, 1.8, ChannelAction::Shrine, ref.id); break;
    case InteractKind::Exit: startChannel(p, MessageId::ChannelExtracting, 4, ChannelAction::Extract, ref.id, true, false); break;
    default: service(s, p, ref.id); break;
  }
}

void regrowNodes(Sim& s) {
  for (std::size_t i = 0; i < s.nodes.size(); ++i) {
    NodeRuntime& n = s.nodes[i];
    if (n.charges <= 0 && s.time >= n.regrowAt) n.charges = n.max;
  }
}

void updateInteract(Sim& s, Player& p, bool pressInteract) {
  if (p.uiOpen || p.cinematic || p.state == PlayerState::Down || p.state == PlayerState::Dead ||
      p.state == PlayerState::Channel)
    return;
  if (!pressInteract || p.state != PlayerState::Free) return;
  const std::optional<Target> best = nearestInRange(s, p);
  if (best && best->runnable) {
    p.cmd.reset();
    runTarget(s, p, best->ref);
  } else if (p.mounted) {
    dismount(s, p, false);
  }
}

void finishChannel(Sim& s, Player& p, const Channel& c) {
  switch (c.action) {
    case ChannelAction::FieldRepair: {
      for (std::optional<Gear>* g : {&p.equip.weapon, &p.equip.armor}) {
        wear(*g, -25);
        if (*g) (*g)->cond = std::min(100.0, (*g)->cond);
      }
      s.toast(p, MessageId::FieldRepairDone, {}, ToastKind::Item);
      s.sfx(p, SoundId::Craft);
      break;
    }
    case ChannelAction::Gather: finishGather(s, p, c.target); break;
    case ChannelAction::Recon: recon(s, p); break;
    case ChannelAction::MountUp: mountUp(s, p); break;
    case ChannelAction::CallMount:
    case ChannelAction::CallMountNear:
      spawnMountNear(s, p);
      mountUp(s, p);
      break;
    case ChannelAction::Extract: leaveTurbulent(s, p, true); break;
    case ChannelAction::Shrine: finishShrine(s, p, c.target); break;
  }
}

}  // namespace rpg::sim
