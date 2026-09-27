// Resolução de dano, abates, golpes em área e projéteis (game/combat.js).
#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/movement/MoveEntity.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::FloatKind;
using protocol::MessageId;
using protocol::MsgArg;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

std::optional<anim::HitOrigin> origin(const std::optional<XZ>& from) {
  if (!from) return std::nullopt;
  return anim::HitOrigin{from->x, from->z};
}

// Reação visual a um golpe: lado de onde veio e o instante. Um golpe novo em menos de 0,35 s não
// reinicia a reação que acabou de começar.
template <class V>
void markHit(const Sim& s, V& v, double yaw, const std::optional<anim::HitOrigin>& from) {
  if (v.hitAt && s.time - *v.hitAt < 0.35) return;
  v.hitDir = anim::hitSide(yaw, v.pos.x, v.pos.z, from);
  v.hitAt = s.time;
}

void moveEnemy(Sim& s, Enemy& e, double dx, double dz, double r) {
  XZ pos{e.pos.x, e.pos.z};
  moveEntity(s.mapOf(e.map), pos, dx, dz, r);
  e.pos.x = pos.x;
  e.pos.z = pos.z;
}

std::uint32_t schoolColor(SpellSchool s) {
  switch (s) {
    case SpellSchool::Ice: return 0x70d6ff;
    case SpellSchool::Fire: return 0xff6b35;
    case SpellSchool::Nature: return 0x52b788;
    case SpellSchool::Wind: return 0x90e0ef;
    case SpellSchool::Curse: return 0x9b5de5;
    case SpellSchool::Holy: return 0xffd166;
    case SpellSchool::Vital: return 0xa7c957;
    default: return 0x9fe2f2;
  }
}

std::uint32_t projectileColor(const Projectile& p) {
  if (p.kind == ProjectileKind::Bolt) return 0xf0e6d2;
  if (p.kind == ProjectileKind::Spell || p.school != SpellSchool::None) return schoolColor(p.school);
  return p.owner == ProjectileOwner::Player ? 0xf3e2b0 : p.owner == ProjectileOwner::Shadow ? 0xd9c4ff : 0xff9a74;
}

// distância de um ponto ao segmento percorrido pelo projétil neste passo
double segDist(double px, double pz, double ax, double az, double bx, double bz) {
  const double dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  const double t = l2 != 0.0 ? std::max(0.0, std::min(1.0, ((px - ax) * dx + (pz - az) * dz) / l2)) : 0.0;
  return jsHypot(ax + dx * t - px, az + dz * t - pz);
}

// Alvo da perseguição quando um inimigo parado é atingido: quem bateu (se for jogador) ou o jogador
// mais próximo no mesmo espaço — na demo, sempre `G.player`.
void wakeTarget(Sim& s, Enemy& e, EntityId attacker) {
  if (Player* p = s.player(attacker); p && p->map == e.map && p->instance == e.instance) {
    e.target = p->id;
    return;
  }
  if (Player* p = s.nearestPlayer(e.map, e.instance, e.pos.x, e.pos.z)) e.target = p->id;
}

}  // namespace

void hurtPlayer(Sim& s, Player& p, double dmg, const DamageSource& src) {
  if (p.state == PlayerState::Down || p.state == PlayerState::Dead || p.cinematic) return;
  if (p.invuln > 0) {
    s.floatText(p.map, p.instance, p.pos.x, p.pos.y + 2.3, p.pos.z, FloatKind::Dodge, MessageId::FloatDodge);
    return;
  }
  p.lastCombat = s.time;
  if (p.shieldHp > 0) {
    const double abs = std::min(p.shieldHp, dmg);
    p.shieldHp -= abs;
    dmg -= abs;
    s.floatText(p.map, p.instance, p.pos.x, p.pos.y + 2.3, p.pos.z, FloatKind::Block, MessageId::FloatShield, {MsgArg::real(abs)});
    s.sfx(p, SoundId::Block);
    if (dmg <= 0) return;
  }
  const double d = dmg;
  if (p.mounted) {
    dismount(s, p, true);
    s.toast(p, MessageId::MountKnockedOff, {}, ToastKind::Warn);
    stagger(p, 0.5);
  }
  cancelChannel(s, p, MessageId::ChannelReasonInterrupted);
  p.hp -= d;
  std::optional<anim::HitOrigin> from;
  if (src.hasPos) from = anim::HitOrigin{src.x, src.z};
  if (d > 0) markHit(s, p, p.yaw, from);
  wear(p.equip.armor, 1.2);
  if (d > 0) {
    s.sfx(p, SoundId::Hurt);
    s.burst(p.map, p.instance, p.pos.x, p.pos.y + 1.3, p.pos.z, 0xc8402e, 7, 4);
    s.floatText(p.map, p.instance, p.pos.x, p.pos.y + 2.3, p.pos.z, FloatKind::Hurt, MessageId::FloatHurt, {MsgArg::real(d)});
    s.toPlayer(p, protocol::EvPlayerHurt{d});
  }
  if (p.hp <= 0) {
    p.hp = 0;
    knockDown(s, p, src);
  }
}

bool hurtEnemy(Sim& s, Enemy& e, double dmg, const HurtOpts& opts) {
  if (!e.alive) return false;
  if (e.curseT > 0) dmg *= 1.35;
  dmg = jsRound(dmg);
  // lado do golpe medido antes do empurrão; dano contínuo (queimadura) não faz o corpo reagir
  if (!opts.dot) markHit(s, e, e.yaw, origin(opts.from));
  if (e.hp - dmg <= 0) {
    e.deathDir = anim::deathFall(e.yaw, e.pos.x, e.pos.z, origin(opts.from));
    e.hasDeathDir = true;
  }
  e.hp -= dmg;
  e.hurtT = 0.18;
  e.barShown = true;
  s.floatText(e.map, e.instance, e.pos.x, e.pos.y + (e.barY != 0 ? e.barY : 2.4), e.pos.z,
              opts.big ? FloatKind::Crit : FloatKind::Dmg, MessageId::FloatNumber, {MsgArg::real(dmg)});
  s.burst(e.map, e.instance, e.pos.x, e.pos.y + 1.1, e.pos.z,
          opts.burstColor.value_or(e.faction == Faction::Shadow ? 0xb48cff : 0xe8c070), 6, 4);
  s.sfxAt(e.map, e.instance, e.pos.x, e.pos.z, SoundId::Hit);
  if (opts.from && opts.knock != 0.0) {
    const double a = js::atan2(e.pos.x - opts.from->x, e.pos.z - opts.from->z);
    moveEnemy(s, e, js::sin(a) * opts.knock, js::cos(a) * opts.knock, e.radius);
  }
  const auto status = [&](MessageId m) { s.floatText(e.map, e.instance, e.pos.x, e.pos.y + 3, e.pos.z, FloatKind::Block, m); };
  if (opts.slow != 0.0) {
    e.slowT = std::max(e.slowT, opts.slow);
    status(MessageId::FloatSlow);
  }
  if (opts.root != 0.0) {
    e.rootT = std::max(e.rootT, opts.root);
    status(MessageId::FloatRoot);
  }
  if (opts.burn != 0.0) {
    e.burnT = std::max(e.burnT, opts.burn);
    if (opts.attacker) e.burnBy = opts.attacker;
  }
  if (opts.curse != 0.0) {
    e.curseT = std::max(e.curseT, opts.curse);
    status(MessageId::FloatCurse);
  }
  if (opts.healPlayer != 0.0) {
    if (Player* p = s.player(opts.attacker)) {
      p->hp = std::min(p->maxHp, p->hp + opts.healPlayer);
      s.floatText(p->map, p->instance, p->pos.x, p->pos.y + 2.3, p->pos.z, FloatKind::Heal, MessageId::FloatHeal,
                  {MsgArg::real(opts.healPlayer)});
    }
  }
  if (opts.stun != 0.0) {
    e.stun = opts.stun;
    s.cancelTelegraph(e.map, e.instance, e.pos.x, e.pos.z, e.tele);
    e.state = EnemyState::Stun;
    status(MessageId::FloatStun);
  } else if (e.state == EnemyState::Windup && s.data.enemies[e.type].faction == Faction::Bandit && dmg >= 20) {
    s.cancelTelegraph(e.map, e.instance, e.pos.x, e.pos.z, e.tele);
    e.state = EnemyState::Recover;
    e.t = 0;
    status(MessageId::FloatInterrupted);
  }
  if (e.state == EnemyState::Idle || e.state == EnemyState::Return || e.state == EnemyState::Loot) {
    e.state = EnemyState::Chase;
    e.t = 0;
    wakeTarget(s, e, opts.byPlayer ? opts.attacker : kNoEntity);
  }
  if (opts.byPlayer) {
    if (Player* p = s.player(opts.attacker)) {
      p->lastCombat = s.time;
      wear(p->equip.weapon, 0.6);
    }
  }
  if (e.hp <= 0) killEnemy(s, e, opts.byPlayer, opts.attacker);
  return true;
}

void killEnemy(Sim& s, Enemy& e, bool byPlayer, EntityId attacker) {
  e.alive = false;
  e.state = EnemyState::Dead;
  e.t = 0;
  e.hp = 0;
  s.cancelTelegraph(e.map, e.instance, e.pos.x, e.pos.z, e.tele);
  const EnemyDef& cfg = s.data.enemies[e.type];
  if (byPlayer) {
    if (Player* p = s.player(attacker)) {
      stat(*p, "kill_" + s.data.enemies.key(e.type));
      stat(*p, "kills");
      if (cfg.coins) {
        const int c = s.rng->rangeInt(cfg.coins->first, cfg.coins->second);
        p->coins += c;
        s.floatText(e.map, e.instance, e.pos.x, e.pos.y + 3, e.pos.z, FloatKind::Coin, MessageId::FloatCoins, {MsgArg::integer(c)});
        s.sfx(*p, SoundId::Coin);
      }
      for (const DropRange& d : cfg.drops) {
        const int q = s.rng->rangeInt(d.min, d.max);
        const int got = receive(s, *p, d.item, q);
        if (got) s.toast(*p, MessageId::ItemReceived, {MsgArg::integer(got), MsgArg::ref(MsgArg::Type::Item, d.item.value)}, ToastKind::Item);
        if (got < q) {
          InvEntry left;
          left.id = d.item;
          left.qty = q - got;
          createLootBag(s, e.map, e.instance, e.pos.x, e.pos.z, {left}, LootLabel::Spoils);
        }
      }
    }
  }
  if (!e.carrying.empty()) {
    createLootBag(s, e.map, e.instance, e.pos.x, e.pos.z, e.carrying, LootLabel::Recoverable);
    s.toNear(e.map, e.instance, e.pos.x, e.pos.z,
             protocol::EvToast{MessageId::BanditDroppedCargo, {}, ToastKind::Item, 0});
    e.carrying.clear();
  }
  // listeners de 'enemy-killed', na ordem em que a demo os registra (acampamento, depois evento)
  onEnemyKilledCamp(s, e, byPlayer, attacker);
  onEnemyKilledEvent(s, e, byPlayer, attacker);
}

int meleeSweep(Sim& s, Player& attacker, double ox, double oz, double yaw, double range, double arc, double dmg,
               HurtOpts opts) {
  const StaticMap& map = s.mapOf(attacker.map);
  opts.from = XZ{ox, oz};
  int n = 0;
  for (std::size_t i = 0; i < s.enemies.size(); ++i) {
    Enemy& e = *s.enemies[i];
    if (!e.alive || e.faction == Faction::Guard || e.map != attacker.map || e.instance != attacker.instance) continue;
    const double dx = e.pos.x - ox, dz = e.pos.z - oz, d = jsHypot(dx, dz);
    if (d > range + e.radius) continue;
    if (std::fabs(e.pos.y - map.groundHeight(ox, oz)) > 3) continue;
    if (arc < 6.2 && d > 1.1 && std::fabs(angleDiff(js::atan2(dx, dz), yaw)) > arc / 2) continue;
    hurtEnemy(s, e, dmg, opts);
    n++;
  }
  return n;
}

void shoot(Sim& s, const ShotSpec& o) {
  auto up = std::make_unique<Projectile>();
  Projectile& p = *up;
  p.id = s.newId();
  p.map = o.map;
  p.instance = o.instance;
  double dx, dy, dz;
  if (o.target) {
    const double vx = o.target->x - o.x, vy = o.target->y - o.y, vz = o.target->z - o.z;
    double len = jsHypot3(vx, vy, vz);
    if (len == 0.0 || std::isnan(len)) len = 1;  // `|| 1`
    dx = vx / len;
    dy = vy / len;
    dz = vz / len;
  } else {
    // sem alvo: inclinação zero (os inimigos atiram na horizontal)
    const double cp = js::cos(0.0);
    dx = js::sin(o.yaw) * cp;
    dy = js::sin(0.0);
    dz = js::cos(o.yaw) * cp;
  }
  p.x = o.x;
  p.y = o.y;
  p.z = o.z;
  p.dx = dx;
  p.dy = dy;
  p.dz = dz;
  p.speed = o.speed;
  p.dmg = o.dmg;
  p.owner = o.owner;
  p.shooter = o.shooter;
  p.pierce = o.pierce;
  p.left = o.range != 0.0 ? o.range : 45;
  p.ox = o.x;
  p.oy = o.y;
  p.oz = o.z;
  p.school = o.school;
  p.kind = o.kind;
  // balística: flechas e virotes do jogador (magias e disparos inimigos seguem em linha reta)
  p.ballistic = o.owner == ProjectileOwner::Player && (o.kind == ProjectileKind::Bolt ||
                                                       (o.kind == ProjectileKind::Arrow && o.school == SpellSchool::None));
  s.projectiles.push_back(std::move(up));
  s.sfxAt(o.map, o.instance, o.x, o.z, o.school != SpellSchool::None ? SoundId::Magic : SoundId::Arrow);
}

void updateProjectiles(Sim& s, double dt) {
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(s.projectiles.size()) - 1; i >= 0; --i) {
    Projectile& p = *s.projectiles[static_cast<std::size_t>(i)];
    const StaticMap& map = s.mapOf(p.map);
    const double step = p.speed * dt;
    const double ax = p.x, az = p.z;
    p.x += p.dx * step;
    p.y += p.dy * step;
    p.z += p.dz * step;
    p.left -= step;

    if (p.ballistic) {
      p.dy -= 9.8 * 0.22 * dt;
      double vlen = jsHypot3(p.dx, p.dy, p.dz);
      if (vlen == 0.0 || std::isnan(vlen)) vlen = 1;
      p.dx /= vlen;
      p.dy /= vlen;
      p.dz /= vlen;
    }

    const double g = map.groundHeight(p.x, p.z);
    const bool hitGround = p.y <= g + 0.12;
    const bool hitWall = map.collision().testPoint(p.x, p.z, 0.28);
    bool dead = p.left <= 0 || hitGround || hitWall;

    if (!dead && p.owner == ProjectileOwner::Player) {
      for (std::size_t k = 0; k < s.enemies.size(); ++k) {
        Enemy& e = *s.enemies[k];
        if (!e.alive || e.faction == Faction::Guard || p.hit.contains(e.id)) continue;
        if (e.map != p.map || e.instance != p.instance) continue;
        const double eBot = e.pos.y, eTop = e.pos.y + (e.barY != 0 ? e.barY : 2.4);
        const bool inY = p.y >= eBot - 0.25 && p.y <= eTop + 0.35;
        if (segDist(e.pos.x, e.pos.z, ax, az, p.x, p.z) < e.radius + 0.55 && inY) {
          p.hit.insert(e.id);
          const bool isCrit = (p.y >= eBot + (eTop - eBot) * 0.68) || p.pierce;
          HurtOpts o;
          o.from = XZ{p.ox, p.oz};
          o.knock = p.pierce ? 1.4 : (p.school == SpellSchool::Wind ? 2.5 : 0.4);
          o.big = isCrit || p.school == SpellSchool::Fire;
          o.burstColor = projectileColor(p);
          if (p.school == SpellSchool::Ice) o.slow = 3.0;
          if (p.school == SpellSchool::Fire) o.burn = 3.0;
          if (p.school == SpellSchool::Nature) o.root = 2.0;
          if (p.school == SpellSchool::Curse) o.curse = 5.0;
          o.attacker = p.shooter;
          hurtEnemy(s, e, isCrit ? jsRound(p.dmg * 1.5) : p.dmg, o);
          if (Player* shooter = s.player(p.shooter)) {
            s.toPlayer(*shooter, protocol::EvHitmarker{isCrit});
          }
          if (!p.pierce) {
            dead = true;
            break;
          }
        }
      }
    } else if (!dead) {
      for (auto& pl : s.players) {
        Player& P = *pl;
        if (P.map != p.map || P.instance != p.instance) continue;
        const double pBot = P.pos.y, pTop = P.pos.y + 1.85;
        const bool inY = p.y >= pBot - 0.2 && p.y <= pTop + 0.3;
        if (segDist(P.pos.x, P.pos.z, ax, az, p.x, p.z) < 0.85 && inY) {
          DamageSource src;
          src.x = p.x - p.dx * 3;
          src.z = p.z - p.dz * 3;
          src.cause = p.school != SpellSchool::None ? MessageId::CauseSpell : MessageId::CauseArrow;
          hurtPlayer(s, P, p.dmg, src);
          dead = true;
          break;
        }
      }
    }
    if (dead) {
      if ((hitGround || hitWall) && p.left > 0) s.burst(p.map, p.instance, p.x, std::max(g + 0.1, p.y), p.z, 0xc8b698, 3, 2);
      s.projectiles.erase(s.projectiles.begin() + i);
    }
  }
}

}  // namespace rpg::sim
