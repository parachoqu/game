#include "client/game/Targets.h"

#include <cmath>
#include <limits>

#include "core/Math.h"

namespace rpg::client {

using protocol::EntityKind;
using protocol::InteractKind;

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

}  // namespace

std::vector<UseTarget> targetsNear(const ClientWorld& w, const StaticWorld& statics, double maxDist) {
  std::vector<UseTarget> list;
  const protocol::EntityState* me = w.me();
  const protocol::PrivateState* self = w.self();
  const protocol::WorldState* world = w.world();
  if (!me || !self || !world) return list;
  const auto near = [&](double x, double z) { return std::hypot(me->x - x, me->z - z) <= maxDist; };
  const GameplayLayout& L = statics.layout();
  if (self->turb.inside) {
    const XZ T = L.turbulent.center;
    for (std::size_t i = 0; i < L.turbulent.shrines.size(); ++i) {
      const bool taken = i < self->turb.shrinesTaken.size() && self->turb.shrinesTaken[i] != 0;
      const double x = T.x + L.turbulent.shrines[i].x, z = T.z + L.turbulent.shrines[i].z;
      if (!taken && near(x, z)) list.push_back({InteractKind::Shrine, static_cast<std::uint32_t>(i), x, z, 3.2, 0, true});
    }
    for (std::size_t i = 0; i < L.turbulent.exits.size(); ++i) {
      const double x = T.x + L.turbulent.exits[i].x, z = T.z + L.turbulent.exits[i].z;
      if (near(x, z)) list.push_back({InteractKind::Exit, static_cast<std::uint32_t>(i), x, z, 3.6, 0, true});
    }
    return list;
  }
  if (static_cast<MapKind>(me->map) != MapKind::Region || me->instance != 0) return list;
  for (std::size_t i = 0; i < L.interactables.size(); ++i) {
    const LayoutInteractable& it = L.interactables[i];
    if (!near(it.x, it.z)) continue;
    const InteractKind kind = kindFromLayout(it.kind);
    UseTarget t{kind, static_cast<std::uint32_t>(i), it.x, it.z, it.r, 0, true};
    t.layoutIndex = static_cast<std::int32_t>(i);
    if (kind == InteractKind::Node) {
      const std::size_t node = static_cast<std::size_t>(it.node);
      t.usable = node < world->nodes.size() ? world->nodes[node].charges > 0 : true;
    } else if (kind == InteractKind::Vigia) {
      const auto rec = world->evtContrib.find("reconhecimento");
      if (rec != world->evtContrib.end() && rec->second > 0) continue;
    }
    list.push_back(t);
  }
  for (const auto& e : w.entities()) {
    if (e.kind == EntityKind::LootBag && near(e.x, e.z)) {
      list.push_back({InteractKind::LootBag, e.id, e.x, e.z, 2.6, 0.5, true});
    } else if (e.kind == EntityKind::Npc && e.type < L.npcs.size() && L.npcs[e.type].traveler >= 0 && near(e.x, e.z)) {
      UseTarget t{InteractKind::Traveler, e.id, e.x, e.z, 3, 0, true};
      t.travelerIndex = L.npcs[e.type].traveler;
      list.push_back(t);
    }
  }
  if (world->portal && near(world->portalX, world->portalZ))
    list.push_back({InteractKind::Portal, world->portalId, world->portalX, world->portalZ, 4.5, 1, true});
  if (self->mountPresent && !self->mounted && near(self->mountX, self->mountZ))
    list.push_back({InteractKind::Mount, 0, self->mountX, self->mountZ, 3.5, 0, true});
  return list;
}

std::optional<UseTarget> nearestInRange(const ClientWorld& w, const StaticWorld& statics) {
  const protocol::EntityState* me = w.me();
  if (!me) return std::nullopt;
  std::optional<UseTarget> best;
  double bd = std::numeric_limits<double>::infinity();
  for (const UseTarget& t : targetsNear(w, statics, 8)) {
    const double d = std::hypot(me->x - t.x, me->z - t.z) - t.bias;
    if (d <= t.r && d < bd) {
      bd = d;
      best = t;
    }
  }
  return best;
}

bool attackable(const protocol::EntityState& e, const GameData& data) {
  if (e.kind != EntityKind::Enemy || !(e.flags & protocol::kFlagAlive) || (e.flags & protocol::kFlagAsleep)) return false;
  const EnemyTypeId type{e.type};
  return !data.enemies.contains(type) || data.enemies[type].faction != Faction::Guard;
}

double enemyBarY(const protocol::EntityState& e) { return (e.flags & protocol::kFlagHuman) ? 2.3 : 1.75 * e.scale; }
double enemyRadius(const protocol::EntityState& e) { return (e.flags & protocol::kFlagHuman) ? 0.45 : 0.7 * e.scale; }

std::optional<Hover> pick(const ClientWorld& w, const StaticWorld& statics, const GameData& data, const ViewCamera& cam,
                          double mx, double my, double width, double height) {
  constexpr double kPickPx = 46;
  const protocol::EntityState* me = w.me();
  if (!me) return std::nullopt;
  std::optional<Hover> best;
  double bd = kPickPx;
  for (const auto& e : w.entities()) {
    if (!attackable(e, data)) continue;
    if (std::hypot(e.x - me->x, e.z - me->z) > 60) continue;
    const auto s = cam.project({e.x, e.y + enemyBarY(e) * 0.45, e.z}, width, height);
    if (!s) continue;
    const double d = std::hypot(s->x - mx, s->y - my);
    if (d < bd) {
      bd = d;
      best = Hover{Hover::Kind::Enemy, e.id, {}};
    }
  }
  if (best) return best;
  bd = kPickPx;
  const StaticMap& map = statics.map(w.map());
  for (const UseTarget& t : targetsNear(w, statics, 45)) {
    if (!t.usable) continue;
    const auto s = cam.project({t.x, map.groundHeight(t.x, t.z) + 1, t.z}, width, height);
    if (!s) continue;
    const double d = std::hypot(s->x - mx, s->y - my);
    if (d < bd) {
      bd = d;
      best = Hover{Hover::Kind::Use, 0, t};
    }
  }
  return best;
}

}  // namespace rpg::client
