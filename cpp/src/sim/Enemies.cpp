// Criaturas, saqueadores, figuras da Turbulenta e guardas: IA e grupos com reaparecimento
// (game/enemies.js).
//
// A demo tinha um só jogador: a IA media distância e escolhia alvo sempre em `G.player`. Aqui o alvo
// é o jogador mais próximo que pode ser atacado, no mesmo mapa e instância; com um jogador só, o
// comportamento é idêntico ao da demo.
#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/movement/MoveEntity.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

bool sameSpace(const Enemy& e, const Player& p) { return e.map == p.map && e.instance == p.instance; }

bool canTarget(const Enemy& e, const Player& p) {
  if (p.state == PlayerState::Down || p.state == PlayerState::Dead || p.cinematic || p.deathPanel) return false;
  if (e.faction == Faction::Bandit && p.zone == ZoneKind::Protegida) return false;
  return true;
}

double distTo(const Enemy& e, const Player& p) { return jsHypot(p.pos.x - e.pos.x, p.pos.z - e.pos.z); }

// Jogador mais próximo que o inimigo pode atacar (nullptr se nenhum).
Player* nearestTargetable(Sim& s, const Enemy& e, double* dist) {
  Player* best = nullptr;
  double bd = 0;
  for (auto& pp : s.players) {
    Player& p = *pp;
    if (!sameSpace(e, p) || !canTarget(e, p)) continue;
    const double d = distTo(e, p);
    if (!best || d < bd) {
      best = &p;
      bd = d;
    }
  }
  if (dist) *dist = best ? bd : std::numeric_limits<double>::infinity();
  return best;
}

double stepToward(Sim& s, Enemy& e, double tx, double tz, double speed, double dt) {
  if (e.rootT > 0) {
    e.rootT = std::max(0.0, e.rootT - dt);
    return 0;
  }
  if (e.slowT > 0) {
    e.slowT = std::max(0.0, e.slowT - dt);
    speed *= 0.52;
  }
  const double dx = tx - e.pos.x, dz = tz - e.pos.z, d = jsHypot(dx, dz);
  if (d < 0.05) return 0;
  const double st = std::min(d, speed * dt);
  const double nx = e.pos.x + dx / d * st, nz = e.pos.z + dz / d * st;
  if (e.faction == Faction::Bandit && zoneOf(s, e.map, nx, nz) == ZoneKind::Protegida) return 0;  // não entram em área protegida
  XZ pos{e.pos.x, e.pos.z};
  moveEntity(s.mapOf(e.map), pos, dx / d * st, dz / d * st, e.radius);
  e.pos.x = pos.x;
  e.pos.z = pos.z;
  e.yaw = js::atan2(dx, dz);
  return st / dt;
}

Enemy* nearestHostile(Sim& s, const Enemy& from, double r) {
  Enemy* best = nullptr;
  double bd = r;
  for (auto& op : s.enemies) {
    Enemy& o = *op;
    if (!o.alive || o.faction == Faction::Guard || o.faction == Faction::Shadow) continue;
    if (o.map != from.map || o.instance != from.instance) continue;
    const double d = jsHypot(o.pos.x - from.pos.x, o.pos.z - from.pos.z);
    if (d < bd) {
      bd = d;
      best = &o;
    }
  }
  return best;
}

double updateGuard(Sim& s, Enemy& e, double dt) {
  const EnemyDef& cfg = s.data.enemies[e.type];
  Enemy* tgt = nearestHostile(s, e, 18);
  double v = 0;
  e.cd -= dt;
  if (tgt) {
    const double d = jsHypot(tgt->pos.x - e.pos.x, tgt->pos.z - e.pos.z);
    if (d > 2.2) {
      v = stepToward(s, e, tgt->pos.x, tgt->pos.z, cfg.speed, dt);
    } else {
      e.yaw = js::atan2(tgt->pos.x - e.pos.x, tgt->pos.z - e.pos.z);
      if (e.cd <= 0) {
        e.cd = 1.3;
        e.atk = 0;
        s.sfxAt(e.map, e.instance, e.pos.x, e.pos.z, SoundId::Swing);
        HurtOpts o;
        o.byPlayer = false;
        o.from = XZ{e.pos.x, e.pos.z};
        o.knock = 0.5;
        hurtEnemy(s, *tgt, cfg.damage, o);
      }
    }
  } else if (e.post) {
    const double d = jsHypot(e.post->x - e.pos.x, e.post->z - e.pos.z);
    if (d > 1) v = stepToward(s, e, e.post->x, e.post->z, cfg.speed * 0.6, dt);
    else e.yaw = e.post->yaw.value_or(e.yaw);
  }
  if (e.atk >= 0) {
    e.atk += dt * 2.2;
    if (e.atk > 1) e.atk = -1;
  }
  return v;
}

void startWindup(Sim& s, Enemy& e, const Player& p, double dP) {
  const EnemyDef& cfg = s.data.enemies[e.type];
  e.state = EnemyState::Windup;
  e.t = 0;
  e.lockYaw = js::atan2(p.pos.x - e.pos.x, p.pos.z - e.pos.z);
  e.yaw = e.lockYaw;
  e.origin = {e.pos.x, e.pos.z};
  protocol::EvTelegraph t;
  t.shape = cfg.shape == AttackShape::Circle ? protocol::TeleShape::Circle
            : cfg.ranged                     ? protocol::TeleShape::Line
                                             : protocol::TeleShape::Sector;
  t.x = e.pos.x;
  t.z = e.pos.z;
  t.radius = cfg.range + 0.3;
  t.arc = cfg.arc;
  t.length = std::min(cfg.range + 4, dP + 6);
  t.width = 1.4;
  t.facing = e.lockYaw;
  t.duration = cfg.windup;
  t.color = 0xff5a3c;
  e.tele = s.telegraph(e.map, e.instance, t);
  if (dP < 26) s.sfxAt(e.map, e.instance, e.pos.x, e.pos.z, SoundId::Warn);
}

void strike(Sim& s, Enemy& e) {
  const EnemyDef& cfg = s.data.enemies[e.type];
  e.tele = 0;  // a telegrafia termina sozinha (duração + 0,12 s no cliente)
  e.state = EnemyState::Recover;
  e.t = 0;
  e.atk = 0;
  if (cfg.ranged) {
    ShotSpec o;
    o.x = e.pos.x + js::sin(e.lockYaw) * 0.8;
    o.y = e.pos.y + 1.45;
    o.z = e.pos.z + js::cos(e.lockYaw) * 0.8;
    o.yaw = e.lockYaw;
    o.speed = 30;
    o.dmg = cfg.damage;
    o.owner = e.faction == Faction::Shadow ? ProjectileOwner::Shadow : ProjectileOwner::Enemy;
    o.range = cfg.range + 8;
    o.map = e.map;
    o.instance = e.instance;
    shoot(s, o);
    return;
  }
  s.sfxAt(e.map, e.instance, e.pos.x, e.pos.z, SoundId::Swing);
  // A demo só testava `G.player`; aqui o golpe atinge qualquer jogador dentro da área.
  for (std::size_t i = 0; i < s.players.size(); ++i) {
    Player& p = *s.players[i];
    if (!sameSpace(e, p)) continue;
    const double dx = p.pos.x - e.origin.x, dz = p.pos.z - e.origin.z, d = jsHypot(dx, dz);
    bool inside;
    if (cfg.shape == AttackShape::Circle) inside = d <= cfg.range + 0.4;
    else inside = d <= cfg.range + 0.6 && (d < 0.8 || std::fabs(angleDiff(js::atan2(dx, dz), e.lockYaw)) <= cfg.arc / 2 + 0.15);
    if (inside && std::fabs(p.pos.y - e.pos.y) < 2.5) {
      DamageSource src;
      src.x = e.origin.x;
      src.z = e.origin.z;
      src.enemyType = e.type;
      src.faction = e.faction;
      hurtPlayer(s, p, cfg.damage, src);
    }
  }
}

double updateHostile(Sim& s, Enemy& e, double dt) {
  const EnemyDef& cfg = s.data.enemies[e.type];
  double dAny = 0;
  Player* nearest = s.nearestPlayer(e.map, e.instance, e.pos.x, e.pos.z, &dAny);
  const double dHome = jsHypot(e.home.x - e.pos.x, e.home.z - e.pos.z);
  // `dP` da demo: medido antes de o inimigo andar neste passo
  double dT = 0;
  Player* cand = nearestTargetable(s, e, &dT);
  double v = 0;
  e.t += dt;
  switch (e.state) {
    case EnemyState::Idle: {
      if (!e.patrol.empty()) {
        const XZ pt = e.patrol[static_cast<std::size_t>(e.pi) % e.patrol.size()];
        if (jsHypot(pt.x - e.pos.x, pt.z - e.pos.z) < 2) e.pi++;
        v = stepToward(s, e, pt.x, pt.z, cfg.speed * 0.45, dt);
      } else {
        if (!e.wander || e.t > 6) {
          const double wx = e.home.x + s.rng->range(-8, 8);
          const double wz = e.home.z + s.rng->range(-8, 8);
          e.wander = XZ{wx, wz};
          e.t = 0;
        }
        if (e.t < 3.5) v = stepToward(s, e, e.wander->x, e.wander->z, cfg.speed * 0.35, dt);
      }
      if (cand && dT < cfg.aggro) {
        e.state = EnemyState::Chase;
        e.t = 0;
        e.target = cand->id;
      } else if (e.faction == Faction::Bandit && e.carrying.empty()) {
        for (auto& bp : s.lootBags) {
          LootBag& b = *bp;
          if (b.claimed || b.items.empty() || b.map != e.map || b.instance != e.instance) continue;
          if (zoneOf(s, b.map, b.x, b.z) == ZoneKind::Protegida) continue;
          if (jsHypot(b.x - e.pos.x, b.z - e.pos.z) >= 75) continue;
          b.claimed = e.id;
          e.targetBag = b.id;
          e.state = EnemyState::Loot;
          break;
        }
      }
      break;
    }
    case EnemyState::Loot: {
      LootBag* b = s.lootBag(e.targetBag);
      if (!b || b->items.empty()) {
        e.state = EnemyState::Idle;
        if (b) b->claimed = kNoEntity;
        break;
      }
      v = stepToward(s, e, b->x, b->z, cfg.speed * 0.8, dt);
      if (jsHypot(b->x - e.pos.x, b->z - e.pos.z) < 1.6) {
        e.carrying = std::move(b->items);
        b->items.clear();
        const double bx = b->x, bz = b->z;
        removeLootBag(s, b->id);
        b = nullptr;
        if (nearest && dAny < 70) {
          s.toNear(e.map, e.instance, bx, bz, protocol::EvToast{MessageId::BanditLootedCargo, {}, ToastKind::Warn, 0}, 70);
        }
        e.state = EnemyState::Idle;
        e.targetBag = kNoEntity;
      }
      if (cand && dT < cfg.aggro * 0.8) {
        e.state = EnemyState::Chase;
        e.target = cand->id;
        if (b) b->claimed = kNoEntity;
      }
      break;
    }
    case EnemyState::Chase: {
      Player* p = s.player(e.target);
      if (!p || !sameSpace(e, *p)) {
        e.state = EnemyState::Return;
        break;
      }
      const double dP = distTo(e, *p);
      if (!canTarget(e, *p) || dHome > e.leash || dP > cfg.aggro * 2.2) {
        e.state = EnemyState::Return;
        break;
      }
      if (cfg.ranged) {
        if (dP < 5.5) v = stepToward(s, e, e.pos.x - (p->pos.x - e.pos.x), e.pos.z - (p->pos.z - e.pos.z), cfg.speed * 0.8, dt);
        else if (dP > cfg.range * 0.85) v = stepToward(s, e, p->pos.x, p->pos.z, cfg.speed, dt);
        else if (e.t > 0.4) startWindup(s, e, *p, dP);
        else e.yaw = js::atan2(p->pos.x - e.pos.x, p->pos.z - e.pos.z);
      } else if (dP > cfg.range * 0.8 + 0.3) {
        v = stepToward(s, e, p->pos.x, p->pos.z, cfg.speed, dt);
      } else {
        startWindup(s, e, *p, dP);
      }
      break;
    }
    case EnemyState::Windup:
      if (e.t >= cfg.windup) strike(s, e);
      break;
    case EnemyState::Recover:
      if (e.atk >= 0) {
        e.atk += dt * 3;
        if (e.atk > 1) e.atk = -1;
      }
      if (e.t > cfg.cooldown * 0.7) {
        e.state = EnemyState::Chase;
        e.t = 0;
      }
      break;
    case EnemyState::Stun:
      e.stun -= dt;
      if (e.stun <= 0) {
        e.state = EnemyState::Chase;
        e.t = 0;
      }
      break;
    case EnemyState::Return:
      v = stepToward(s, e, e.home.x, e.home.z, cfg.speed * 1.1, dt);
      e.hp = std::min(e.maxHp, e.hp + e.maxHp * 0.25 * dt);
      if (dHome < 2.5) {
        e.state = EnemyState::Idle;
        e.t = 0;
        e.barShown = false;
      }
      break;
    case EnemyState::Dead: break;
  }
  return v;
}

void separate(Sim& s) {
  auto& list = s.enemies;
  for (std::size_t i = 0; i < list.size(); ++i) {
    Enemy& a = *list[i];
    if (!a.alive) continue;
    for (std::size_t j = i + 1; j < list.size(); ++j) {
      Enemy& b = *list[j];
      if (!b.alive || b.map != a.map || b.instance != a.instance) continue;
      const double dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z, d = jsHypot(dx, dz), m = a.radius + b.radius;
      if (d < m && d > 1e-3) {
        const double k = (m - d) / 2 / d;
        a.pos.x -= dx * k;
        a.pos.z -= dz * k;
        b.pos.x += dx * k;
        b.pos.z += dz * k;
      }
    }
  }
}

void spawnGroup(Sim& s, int gi) {
  GroupRuntime& g = s.groups[static_cast<std::size_t>(gi)];
  const LayoutGroup& def = s.layout.groups[static_cast<std::size_t>(g.layoutIndex)];
  g.list.clear();
  for (const LayoutGroup::Member& m : def.members) {
    SpawnOpts o;
    o.group = gi;
    o.leash = def.leash;
    const Enemy& e = spawnEnemy(s, enemyType(s, m.type), def.x + m.dx, def.z + m.dz, o);
    s.groups[static_cast<std::size_t>(gi)].list.push_back(e.id);
  }
  s.groups[static_cast<std::size_t>(gi)].deadAt.reset();
}

}  // namespace

Enemy& spawnEnemy(Sim& s, EnemyTypeId type, double x, double z, const SpawnOpts& o) {
  const EnemyDef& cfg = s.data.enemies[type];
  auto up = std::make_unique<Enemy>();
  Enemy& e = *up;
  e.id = s.newId();
  e.type = type;
  e.faction = cfg.faction;
  e.map = o.map;
  e.instance = o.instance;
  e.alive = true;
  e.state = EnemyState::Idle;
  e.t = s.rng->range(0, 2);
  e.pos = {x, s.mapOf(o.map).groundHeight(x, z), z};
  e.yaw = s.rng->range(0, kPi * 2);
  e.hp = cfg.hp;
  e.maxHp = cfg.hp;
  e.home = {x, z};
  e.leash = o.leash && *o.leash != 0 ? *o.leash : 42;
  e.radius = cfg.faction == Faction::Beast ? 0.7 * cfg.scale : 0.45;
  e.group = o.group;
  e.barY = cfg.faction == Faction::Beast ? 1.75 * cfg.scale : 2.3;
  e.post = o.post;
  e.patrol = o.patrol;
  s.enemies.push_back(std::move(up));
  return e;
}

void removeEnemy(Sim& s, EntityId id) {
  for (auto it = s.enemies.begin(); it != s.enemies.end(); ++it) {
    if ((*it)->id != id) continue;
    Enemy& e = **it;
    s.cancelTelegraph(e.map, e.instance, e.pos.x, e.pos.z, e.tele);
    s.enemies.erase(it);
    return;
  }
}

void defineGroups(Sim& s) {
  for (std::size_t i = 0; i < s.layout.groups.size(); ++i) {
    GroupRuntime g;
    g.layoutIndex = static_cast<int>(i);
    s.groups.push_back(g);
    spawnGroup(s, static_cast<int>(s.groups.size()) - 1);
  }
}

void updateGroups(Sim& s) {
  for (std::size_t gi = 0; gi < s.groups.size(); ++gi) {
    GroupRuntime& g = s.groups[gi];
    const LayoutGroup& def = s.layout.groups[static_cast<std::size_t>(g.layoutIndex)];
    bool anyAlive = false;
    for (const EntityId id : g.list) {
      if (const Enemy* e = s.enemy(id); e && e->alive) {
        anyAlive = true;
        break;
      }
    }
    if (anyAlive) continue;
    if (!g.deadAt) g.deadAt = s.time;
    // world.js `gorgeActive`: enquanto a Passagem não reabre, sempre; depois, 35% das vezes que o
    // grupo é consultado (o sorteio acontece a cada passo, como na demo)
    const bool active = !def.gorge || !s.evt.done || s.rng->next() < 0.35;
    if (!active) continue;
    bool farFromAll = true;
    for (const auto& p : s.players) {
      if (p->map != MapKind::Region || p->instance != kRegionInstance) continue;
      if (jsHypot(p->pos.x - def.x, p->pos.z - def.z) <= 55) farFromAll = false;
    }
    if (s.time - *g.deadAt > def.respawn && farFromAll) spawnGroup(s, static_cast<int>(gi));
  }
}

void updateEnemies(Sim& s, double dt) {
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(s.enemies.size()) - 1; i >= 0; --i) {
    if (static_cast<std::size_t>(i) >= s.enemies.size()) continue;
    Enemy& e = *s.enemies[static_cast<std::size_t>(i)];
    double dP = 0;
    s.nearestPlayer(e.map, e.instance, e.pos.x, e.pos.z, &dP);
    if (!e.alive) {
      e.t += dt;
      e.speedNow = 0;
      if (e.t > 7) removeEnemy(s, e.id);
      continue;
    }
    if (dP > 110 && e.state == EnemyState::Idle) {
      e.asleep = true;
      continue;
    }
    e.asleep = false;
    if (e.burnT > 0) {
      e.burnT = std::max(0.0, e.burnT - dt);
      e.burnTick += dt;
      if (e.burnTick >= 0.5) {
        e.burnTick = 0;
        HurtOpts o;
        o.byPlayer = true;
        o.attacker = e.burnBy;
        o.burstColor = 0xff6b35;
        o.dot = true;
        hurtEnemy(s, e, 5, o);
      }
    }
    if (e.curseT > 0) e.curseT = std::max(0.0, e.curseT - dt);
    const double v = e.faction == Faction::Guard ? updateGuard(s, e, dt) : updateHostile(s, e, dt);
    e.hurtT = std::max(0.0, e.hurtT - dt);
    e.pos.y = s.mapOf(e.map).groundHeight(e.pos.x, e.pos.z);
    e.speedNow = v;
  }
  separate(s);
}

}  // namespace rpg::sim
