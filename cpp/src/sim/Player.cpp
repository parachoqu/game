// Jogador (game/player.js): criação, estados, ações, técnicas e o passo `updatePlayer`.
//
// Porte linha a linha: a ordem das operações, das chamadas a `moveEntity` e dos sorteios é a da
// demo. O que era do cliente (mira por raio da câmera, câmera, clipes, modelo) saiu: a mira chega
// pronta no comando e o resto vira evento ou estado replicado.
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/movement/MoveEntity.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

constexpr std::uint32_t kTeal = 0x5fd6c4;
constexpr std::uint32_t kIce = 0x70d6ff;
constexpr std::uint32_t kFire = 0xff6b35;
constexpr std::uint32_t kHoly = 0xffd166;

const std::string& skillKey(const Sim& s, SkillId id) { return s.data.skills.key(id); }

XZ xz(const Vec3d& v) { return {v.x, v.z}; }

void move(Sim& s, Player& p, double dx, double dz, double r, double lift = 0.0) {
  XZ pos = xz(p.pos);
  moveEntity(s.mapOf(p.map), pos, dx, dz, r, lift);
  p.pos.x = pos.x;
  p.pos.z = pos.z;
}

void moveEnemy(Sim& s, Enemy& e, double dx, double dz, double r) {
  XZ pos{e.pos.x, e.pos.z};
  moveEntity(s.mapOf(e.map), pos, dx, dz, r);
  e.pos.x = pos.x;
  e.pos.z = pos.z;
}

bool sameSpace(const Player& p, const Enemy& e) { return p.map == e.map && p.instance == e.instance; }

void recoil(Sim& s, Player& p, double pitch, double jitter) { s.toPlayer(p, protocol::EvCamRecoil{pitch, jitter}); }
void shake(Sim& s, Player& p, double amount) { s.toPlayer(p, protocol::EvCamShake{amount}); }

void sound(Sim& s, const Player& p, SoundId snd) { s.sfxAt(p.map, p.instance, p.pos.x, p.pos.z, snd); }

void tele(Sim& s, Player& p, protocol::TeleShape shape, double x, double z, double radius, double length, double width,
          double facing, double duration, std::uint32_t color) {
  protocol::EvTelegraph t;
  t.shape = shape;
  t.x = x;
  t.z = z;
  t.radius = radius;
  t.length = length;
  t.width = width;
  t.facing = facing;
  t.duration = duration;
  t.color = color;
  s.telegraph(p.map, p.instance, t);
}

void burstAt(Sim& s, const Player& p, double x, double y, double z, std::uint32_t color, int n, double speed) {
  s.burst(p.map, p.instance, x, y, z, color, n, speed);
}

void floatAt(Sim& s, const Player& p, double x, double y, double z, protocol::FloatKind k, MessageId m,
             protocol::MsgArgs args = {}) {
  s.floatText(p.map, p.instance, x, y, z, k, m, std::move(args));
}

// Todos os inimigos vivos, fora os guardas, no mesmo espaço do jogador (o `for (const e of G.enemies)`
// das técnicas). Itera por índice: um guarda criado no meio do laço (reabertura da Passagem) entra no
// fim da lista e é visitado, como no for-of do JS.
template <class Fn>
void forHostiles(Sim& s, const Player& p, Fn&& fn) {
  for (std::size_t i = 0; i < s.enemies.size(); ++i) {
    Enemy& e = *s.enemies[i];
    if (!e.alive || e.faction == Faction::Guard || !sameSpace(p, e)) continue;
    if (!fn(e)) break;
  }
}

HurtOpts byPlayer(const Player& p) {
  HurtOpts o;
  o.attacker = p.id;
  return o;
}

// ---------------------------------------------------------------- mira e disparos
double assistYaw(Sim& s, const Player& p, double yaw, double range = 40, double tol = 0.11) {
  if (const Enemy* ae = s.enemy(p.aimEnemy); ae && ae->alive && ae->faction != Faction::Guard && sameSpace(p, *ae)) {
    const double dx = ae->pos.x - p.pos.x, dz = ae->pos.z - p.pos.z;
    return js::atan2(dx, dz);
  }
  std::optional<double> best;
  double bd = tol;
  for (const auto& ep : s.enemies) {
    const Enemy& e = *ep;
    if (!e.alive || e.faction == Faction::Guard || !sameSpace(p, e)) continue;
    const double dx = e.pos.x - p.pos.x, dz = e.pos.z - p.pos.z, d = jsHypot(dx, dz);
    if (d > range || d < 1) continue;
    const double a = std::fabs(angleDiff(js::atan2(dx, dz), yaw));
    if (a < bd) {
      bd = a;
      best = js::atan2(dx, dz);
    }
  }
  return best.value_or(yaw);
}

ShotSpec playerShot(const Player& p, double yaw, double speed, double dmg, double range) {
  ShotSpec o;
  o.x = p.pos.x + js::sin(yaw) * 0.8;
  o.y = p.pos.y + 1.45;
  o.z = p.pos.z + js::cos(yaw) * 0.8;
  o.yaw = yaw;
  o.speed = speed;
  o.dmg = dmg;
  o.owner = ProjectileOwner::Player;
  o.shooter = p.id;
  o.range = range;
  o.target = p.aimPoint;
  o.map = p.map;
  o.instance = p.instance;
  return o;
}

// ---------------------------------------------------------------- ataque básico
void basicAttack(Sim& s, Player& p) {
  const WeaponFamilyId fam = weaponFamily(s, p);
  const std::string& famKey = s.data.weapons.key(fam);
  const BasicAttack& b = s.data.weapons[fam].basic;
  if (p.equip.weapon && p.equip.weapon->cond <= 0 && !p.warnedBroken) {
    s.toast(p, MessageId::WeaponBroken, {}, ToastKind::Warn);
    p.warnedBroken = true;
  }
  p.yaw = p.aimYaw;
  p.state = PlayerState::Attack;
  p.stateT = 0;
  ActionKind kind = ActionKind::Swing;
  if (b.type == AttackType::Ranged) kind = famKey == "besta" ? ActionKind::Crossbow : ActionKind::Bow;
  else if (b.type == AttackType::Spell) kind = ActionKind::Cast;
  else if (famKey == "manoplas") kind = ActionKind::Rapid;
  else if (famKey == "machado" || famKey == "maca") kind = ActionKind::Heavy;
  else if (famKey == "lanca") kind = ActionKind::Thrust;
  else if (famKey == "foice" || famKey == "adaga") kind = ActionKind::Slash;
  BasicAct act;
  act.kind = kind;
  act.windup = b.windup;
  act.recover = b.recover;
  act.done = false;
  act.family = fam;
  act.aimed = p.aiming;
  p.basic = act;
  p.skill.reset();
}

void resolveBasic(Sim& s, Player& p) {
  const WeaponFamilyId fam = p.basic->family;
  const std::string& famKey = s.data.weapons.key(fam);
  const BasicAttack& b = s.data.weapons[fam].basic;
  const double tol = p.basic->aimed ? 0.04 : 0.11;  // mirando, quase não há ímã
  if (b.type == AttackType::Ranged) {
    const double yaw = assistYaw(s, p, p.yaw, 40, tol);
    ShotSpec o = playerShot(p, yaw, b.speed, b.damage, 44);
    o.kind = famKey == "besta" ? ProjectileKind::Bolt : ProjectileKind::Arrow;
    shoot(s, o);
    recoil(s, p, famKey == "besta" ? 0.038 : 0.026, 0.012);
  } else if (b.type == AttackType::Spell) {
    const double yaw = assistYaw(s, p, p.yaw, 40, tol);
    ShotSpec o = playerShot(p, yaw, b.speed, b.damage, 40);
    o.kind = ProjectileKind::Spell;
    o.school = b.school;
    shoot(s, o);
    recoil(s, p, 0.028, 0.01);
  } else {
    sound(s, p, famKey == "maca" || famKey == "martelo" ? SoundId::Slam : famKey == "metamorfose" ? SoundId::Roar : SoundId::Swing);
    const double knock = famKey == "maca" ? 0.9 : famKey == "machado" ? 0.8 : famKey == "martelo" ? 0.9 : 0.4;
    HurtOpts o = byPlayer(p);
    o.knock = knock;
    meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, b.range, b.arc, b.damage, o);
  }
}

// ---------------------------------------------------------------- técnicas
void useSkill(Sim& s, Player& p, int slot) {
  const WeaponFamilyId fam = weaponFamily(s, p);
  const auto& skills = s.data.weapons[fam].skills;
  const std::size_t idx = slot == 0 ? 0 : 1;
  if (idx >= skills.size()) {
    s.toast(p, MessageId::SkillNone, {}, ToastKind::Info);
    return;
  }
  const SkillId id = skills[idx];
  const SkillDef& sd = s.data.skills[id];
  double& cd = slot == 0 ? p.cd.q : p.cd.e;
  if (cd > 0) {
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (p.vigor < sd.cost) {
    s.toast(p, MessageId::VigorInsufficient, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  p.vigor -= sd.cost;
  p.vigorDelay = 0.7;
  cd = sd.cooldown;
  p.yaw = p.aimYaw;
  p.state = PlayerState::Skill;
  p.stateT = 0;
  SkillAct act;
  act.skill = id;
  p.skill = act;
  p.basic.reset();
  const std::string& k = skillKey(s, id);
  stat(p, "skill_" + k);

  using protocol::TeleShape;
  const double fx = p.pos.x + js::sin(p.yaw) * 4, fz = p.pos.z + js::cos(p.yaw) * 4;
  if (k == "giro" || k == "ceifaCircular") {
    tele(s, p, TeleShape::Circle, p.pos.x, p.pos.z, 3.4, 0, 0, 0, 0.35, kTeal);
  } else if (k == "ondaChoque") {
    tele(s, p, TeleShape::Circle, p.pos.x, p.pos.z, 3.8, 0, 0, 0, 0.45, 0xe3b24c);
  } else if (k == "prisaoGelo") {
    tele(s, p, TeleShape::Circle, fx, fz, 3.6, 0, 0, 0, 0.5, kIce);
  } else if (k == "erupcaoFogo") {
    tele(s, p, TeleShape::Circle, fx, fz, 3.6, 0, 0, 0, 0.5, kFire);
  } else if (k == "rugidoFera") {
    tele(s, p, TeleShape::Circle, p.pos.x, p.pos.z, 5.0, 0, 0, 0, 0.35, 0xd4a373);
  } else if (k == "purificacaoSagrada") {
    tele(s, p, TeleShape::Circle, p.pos.x, p.pos.z, 4.8, 0, 0, 0, 0.4, kHoly);
  } else if (k == "tiroCarregado" || k == "tiroBesta") {
    tele(s, p, TeleShape::Line, p.pos.x, p.pos.z, 0, 38, 1.2, p.yaw, 0.6, kTeal);
  } else if (k == "estocadaLonga") {
    tele(s, p, TeleShape::Line, p.pos.x, p.pos.z, 0, 4.5, 1.2, p.yaw, 0.3, 0xe3b24c);
  } else if (k == "reparoCampo") {
    p.state = PlayerState::Free;
    startChannel(p, MessageId::ChannelFieldRepair, 1.5, ChannelAction::FieldRepair);
  }
}

// Disparo de técnica com assistência de mira (tiroCarregado, tiroBesta, setaGelo…).
void skillShot(Sim& s, Player& p, double range, double speed, double dmg, ProjectileKind kind, SpellSchool school,
               bool pierce, double shotRange, double assistRange) {
  const double y = assistYaw(s, p, p.yaw, assistRange);
  ShotSpec o = playerShot(p, y, speed, dmg, shotRange);
  (void)range;
  o.kind = kind;
  o.school = school;
  o.pierce = pierce;
  shoot(s, o);
}

void updateSkill(Sim& s, Player& p, double dt) {
  SkillAct& a = *p.skill;
  const double t = p.stateT;
  const std::string& k = skillKey(s, a.skill);
  const auto free = [&] { p.state = PlayerState::Free; };

  if (k == "investida") {
    if (t > 0.1 && t < 0.38) {
      p.invuln = 0.05;
      move(s, p, js::sin(p.yaw) * 26 * dt, js::cos(p.yaw) * 26 * dt, 0.45);
      forHostiles(s, p, [&](Enemy& e) {
        if (a.hitSet.contains(e.id)) return true;
        if (jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z) < 1.8 + e.radius) {
          a.hitSet.insert(e.id);
          HurtOpts o = byPlayer(p);
          o.from = xz(p.pos);
          o.knock = 2;
          o.big = true;
          hurtEnemy(s, e, 22, o);
        }
        return true;
      });
    }
    if (t > 0.1 && !a.swooshed) {
      a.swooshed = true;
      sound(s, p, SoundId::Dodge);
    }
    if (t > 0.55) free();
  } else if (k == "giro") {
    if (t >= 0.35 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      HurtOpts o = byPlayer(p);
      o.knock = 2.5;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 3.4, 7, 18, o);
    }
    if (t > 0.75) free();
  } else if (k == "tiroCarregado") {
    if (t >= 0.6 && !a.done) {
      a.done = true;
      skillShot(s, p, 48, 62, 30, ProjectileKind::Arrow, SpellSchool::None, true, 48, 48);
      recoil(s, p, 0.045, 0.015);
      shake(s, p, 0.35);
    }
    if (t > 0.85) free();
  } else if (k == "recuo") {
    if (t < 0.25) {
      p.invuln = 0.05;
      move(s, p, -js::sin(p.yaw) * 20 * dt, -js::cos(p.yaw) * 20 * dt, 0.45);
    }
    if (t >= 0.25 && !a.done) {
      a.done = true;
      ShotSpec o = playerShot(p, 0, 46, 12, 40);
      o.x = p.pos.x + js::sin(p.yaw) * 0.8;
      o.z = p.pos.z + js::cos(p.yaw) * 0.8;
      o.yaw = assistYaw(s, p, p.aimYaw);
      shoot(s, o);
      recoil(s, p, 0.03, 0.01);
    }
    if (t > 0.5) free();
  } else if (k == "atordoar") {
    if (t >= 0.45 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      HurtOpts o = byPlayer(p);
      o.stun = 1.5;
      o.knock = 0.6;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 2.7, 1.9, 10, o);
    }
    if (t > 0.85) free();
  } else if (k == "comboManopla") {
    const auto hitStep = [&](double knock, double range, double arc, double dmg, bool big, SoundId snd, bool step) {
      sound(s, p, snd);
      HurtOpts o = byPlayer(p);
      o.knock = knock;
      o.big = big;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, range, arc, dmg, o);
      if (step) move(s, p, js::sin(p.yaw) * 4 * dt, js::cos(p.yaw) * 4 * dt, 0.45);
    };
    if (t >= 0.12 && a.subStep == 0) {
      a.subStep = 1;
      hitStep(0.2, 2.3, 1.8, 9, false, SoundId::Swing, true);
    }
    if (t >= 0.25 && a.subStep == 1) {
      a.subStep = 2;
      hitStep(0.3, 2.3, 1.8, 9, false, SoundId::Swing, true);
    }
    if (t >= 0.38 && a.subStep == 2) {
      a.subStep = 3;
      a.done = true;
      hitStep(1.5, 2.4, 2.0, 14, true, SoundId::Hit, false);
    }
    if (t > 0.55) free();
  } else if (k == "avancoImpacto") {
    if (t > 0.08 && t < 0.28) {
      p.invuln = 0.05;
      move(s, p, js::sin(p.yaw) * 24 * dt, js::cos(p.yaw) * 24 * dt, 0.45);
      forHostiles(s, p, [&](Enemy& e) {
        if (a.hitSet.contains(e.id)) return true;
        if (jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z) < 1.7 + e.radius) {
          a.hitSet.insert(e.id);
          HurtOpts o = byPlayer(p);
          o.from = xz(p.pos);
          o.knock = 2.2;
          o.stun = 1.4;
          o.big = true;
          hurtEnemy(s, e, 24, o);
          sound(s, p, SoundId::Slam);
        }
        return true;
      });
    }
    if (t > 0.55) free();
  } else if (k == "golpeEsmagador") {
    if (t >= 0.4 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Slam);
      HurtOpts o = byPlayer(p);
      o.slow = 3.5;
      o.knock = 1.4;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 2.7, 1.8, 26, o);
    }
    if (t > 0.7) free();
  } else if (k == "ondaChoque") {
    if (t >= 0.45 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Slam);
      burstAt(s, p, p.pos.x, p.pos.y + 0.3, p.pos.z, 0xc49a45, 18, 5);
      HurtOpts o = byPlayer(p);
      o.knock = 3.2;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 3.8, 7.0, 18, o);
    }
    if (t > 0.75) free();
  } else if (k == "machadadaPesada") {
    if (t >= 0.45 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Hit);
      HurtOpts o = byPlayer(p);
      o.knock = 1.8;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 2.8, 1.6, 34, o);
    }
    if (t > 0.75) free();
  } else if (k == "dilacerar") {
    if (t >= 0.28 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      HurtOpts o = byPlayer(p);
      o.burn = 3.5;
      o.knock = 0.8;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 2.8, 3.2, 18, o);
    }
    if (t > 0.6) free();
  } else if (k == "passoSombras") {
    if (!a.done) {
      a.done = true;
      p.invuln = 0.25;
      sound(s, p, SoundId::Dodge);
      move(s, p, js::sin(p.aimYaw) * 6.5, js::cos(p.aimYaw) * 6.5, 0.45);
      burstAt(s, p, p.pos.x, p.pos.y + 1, p.pos.z, 0xb0c4de, 10, 4);
      HurtOpts o = byPlayer(p);
      o.knock = 0.5;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.aimYaw, 2.4, 2.0, 22, o);
    }
    if (t > 0.45) free();
  } else if (k == "golpePreciso") {
    if (t >= 0.22 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Hit);
      HurtOpts o = byPlayer(p);
      o.knock = 0.6;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 2.3, 1.4, 28, o);
    }
    if (t > 0.45) free();
  } else if (k == "estocadaLonga") {
    if (t >= 0.3 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      HurtOpts o = byPlayer(p);
      o.knock = 2.8;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 4.4, 1.1, 24, o);
    }
    if (t > 0.6) free();
  } else if (k == "varrerDistancia") {
    if (t >= 0.32 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      HurtOpts o = byPlayer(p);
      o.knock = 2.4;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 3.6, 3.4, 17, o);
    }
    if (t > 0.65) free();
  } else if (k == "ceifaCircular") {
    if (t >= 0.35 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      burstAt(s, p, p.pos.x, p.pos.y + 0.8, p.pos.z, 0x8b008b, 12, 3);
      HurtOpts o = byPlayer(p);
      o.knock = 1.0;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 3.3, 7.0, 22, o);
    }
    if (t > 0.75) free();
  } else if (k == "puxaoFoice") {
    if (t >= 0.28 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Swing);
      forHostiles(s, p, [&](Enemy& e) {
        const double d = jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z);
        if (d < 7.5 && std::fabs(angleDiff(js::atan2(e.pos.x - p.pos.x, e.pos.z - p.pos.z), p.yaw)) < 0.7) {
          HurtOpts o = byPlayer(p);
          o.big = true;
          hurtEnemy(s, e, 16, o);
          const double toX = p.pos.x + js::sin(p.yaw) * 1.8;
          const double toZ = p.pos.z + js::cos(p.yaw) * 1.8;
          moveEnemy(s, e, toX - e.pos.x, toZ - e.pos.z, e.radius);
          return false;
        }
        return true;
      });
    }
    if (t > 0.6) free();
  } else if (k == "tiroBesta") {
    if (t >= 0.38 && !a.done) {
      a.done = true;
      skillShot(s, p, 45, 68, 34, ProjectileKind::Bolt, SpellSchool::None, true, 45, 45);
      recoil(s, p, 0.045, 0.012);
    }
    if (t > 0.7) free();
  } else if (k == "disparoRepulsao") {
    if (t >= 0.25 && !a.done) {
      a.done = true;
      skillShot(s, p, 15, 55, 22, ProjectileKind::Bolt, SpellSchool::None, false, 18, 15);
      HurtOpts o = byPlayer(p);
      o.knock = 4.5;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 3.2, 1.6, 15, o);
      recoil(s, p, 0.035, 0.01);
    }
    if (t > 0.55) free();
  } else if (k == "setaGelo") {
    if (t >= 0.28 && !a.done) {
      a.done = true;
      skillShot(s, p, 40, 42, 18, ProjectileKind::Spell, SpellSchool::Ice, false, 45, 40);
      recoil(s, p, 0.026, 0.01);
    }
    if (t > 0.55) free();
  } else if (k == "prisaoGelo" || k == "erupcaoFogo") {
    const bool ice = k == "prisaoGelo";
    if (t >= 0.5 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      const double cx = p.pos.x + js::sin(p.yaw) * 4, cz = p.pos.z + js::cos(p.yaw) * 4;
      burstAt(s, p, cx, p.pos.y + 0.5, cz, ice ? 0x70d6ff : 0xff6b35, ice ? 24 : 26, 6);
      forHostiles(s, p, [&](Enemy& e) {
        if (jsHypot(e.pos.x - cx, e.pos.z - cz) <= 3.8) {
          HurtOpts o = byPlayer(p);
          o.big = true;
          if (ice) {
            o.stun = 2.0;
            o.burstColor = 0x70d6ff;
            hurtEnemy(s, e, 22, o);
          } else {
            o.burn = 3.5;
            o.burstColor = 0xff6b35;
            o.knock = 1.5;
            hurtEnemy(s, e, 32, o);
          }
        }
        return true;
      });
    }
    if (t > (ice ? 0.7 : 0.75)) free();
  } else if (k == "bolaFogo") {
    if (t >= 0.32 && !a.done) {
      a.done = true;
      skillShot(s, p, 40, 45, 28, ProjectileKind::Spell, SpellSchool::Fire, false, 45, 40);
      recoil(s, p, 0.04, 0.015);
      shake(s, p, 0.3);
    }
    if (t > 0.65) free();
  } else if (k == "enraizar") {
    if (t >= 0.3 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      forHostiles(s, p, [&](Enemy& e) {
        const double d = jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z);
        if (d <= 14 && std::fabs(angleDiff(js::atan2(e.pos.x - p.pos.x, e.pos.z - p.pos.z), p.yaw)) < 0.8) {
          s.burst(e.map, e.instance, e.pos.x, e.pos.y + 0.2, e.pos.z, 0x52b788, 16, 3);
          HurtOpts o = byPlayer(p);
          o.root = 2.8;
          o.burstColor = 0x52b788;
          o.big = true;
          hurtEnemy(s, e, 16, o);
          return false;
        }
        return true;
      });
    }
    if (t > 0.6) free();
  } else if (k == "bencaoTerra") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Pickup);
      p.vigor = std::min(p.maxVigor, p.vigor + 45);
      p.hp = std::min(p.maxHp, p.hp + 18);
      p.invuln = 1.2;
      burstAt(s, p, p.pos.x, p.pos.y + 1, p.pos.z, 0x52b788, 15, 3);
      floatAt(s, p, p.pos.x, p.pos.y + 2.3, p.pos.z, protocol::FloatKind::Heal, MessageId::FloatLifeGain, {MsgArg::integer(18)});
    }
    if (t > 0.55) free();
  } else if (k == "lufadaVento") {
    if (t >= 0.22 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      burstAt(s, p, p.pos.x + js::sin(p.yaw) * 2, p.pos.y + 1, p.pos.z + js::cos(p.yaw) * 2, 0x90e0ef, 20, 5);
      HurtOpts o = byPlayer(p);
      o.knock = 5.0;
      o.big = true;
      meleeSweep(s, p, p.pos.x, p.pos.z, p.yaw, 5.0, 1.8, 15, o);
    }
    if (t > 0.55) free();
  } else if (k == "saltoVento") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Dodge);
      p.invuln = 0.35;
      move(s, p, js::sin(p.aimYaw) * 8.0, js::cos(p.aimYaw) * 8.0, 0.45);
      burstAt(s, p, p.pos.x, p.pos.y + 1, p.pos.z, 0x90e0ef, 14, 4);
    }
    if (t > 0.5) free();
  } else if (k == "marcaCorruptora") {
    if (t >= 0.28 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      skillShot(s, p, 35, 40, 14, ProjectileKind::Spell, SpellSchool::Curse, false, 45, 35);
      recoil(s, p, 0.025, 0.01);
    }
    if (t > 0.6) free();
  } else if (k == "drenoVital") {
    if (t >= 0.35 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      forHostiles(s, p, [&](Enemy& e) {
        const double d = jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z);
        if (d <= 12 && std::fabs(angleDiff(js::atan2(e.pos.x - p.pos.x, e.pos.z - p.pos.z), p.yaw)) < 0.7) {
          HurtOpts o = byPlayer(p);
          o.healPlayer = 26;
          o.burstColor = 0x9b5de5;
          o.big = true;
          hurtEnemy(s, e, 26, o);
          burstAt(s, p, p.pos.x, p.pos.y + 1.2, p.pos.z, 0x9b5de5, 12, 3);
          return false;
        }
        return true;
      });
    }
    if (t > 0.7) free();
  } else if (k == "formaFera") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Roar);
      p.metamorphT = 14;
      p.hp = std::min(p.maxHp, p.hp + 25);
      burstAt(s, p, p.pos.x, p.pos.y + 1, p.pos.z, 0xbc6c25, 20, 4);
      floatAt(s, p, p.pos.x, p.pos.y + 2.4, p.pos.z, protocol::FloatKind::Crit, MessageId::FloatBeastForm);
    }
    if (t > 0.6) free();
  } else if (k == "rugidoFera") {
    if (t >= 0.25 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Roar);
      burstAt(s, p, p.pos.x, p.pos.y + 1.2, p.pos.z, 0xd4a373, 25, 5);
      forHostiles(s, p, [&](Enemy& e) {
        if (jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z) <= 5.2) {
          HurtOpts o = byPlayer(p);
          o.stun = 1.8;
          o.knock = 1.5;
          o.big = true;
          hurtEnemy(s, e, 14, o);
        }
        return true;
      });
    }
    if (t > 0.65) free();
  } else if (k == "curaVital") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Pickup);
      p.hp = std::min(p.maxHp, p.hp + 50);
      burstAt(s, p, p.pos.x, p.pos.y + 1.2, p.pos.z, 0x74c69d, 18, 3);
      floatAt(s, p, p.pos.x, p.pos.y + 2.3, p.pos.z, protocol::FloatKind::Heal, MessageId::FloatLifeGain, {MsgArg::integer(50)});
    }
    if (t > 0.6) free();
  } else if (k == "auraRestauracao") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Pickup);
      p.auraVitalT = 5.0;
      burstAt(s, p, p.pos.x, p.pos.y + 0.3, p.pos.z, 0xa7c957, 20, 3);
      floatAt(s, p, p.pos.x, p.pos.y + 2.3, p.pos.z, protocol::FloatKind::Heal, MessageId::FloatAura);
    }
    if (t > 0.65) free();
  } else if (k == "escudoRadiante") {
    if (!a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      p.shieldHp = 50;
      p.shieldT = 8.0;
      burstAt(s, p, p.pos.x, p.pos.y + 1.2, p.pos.z, 0xffd166, 22, 4);
      floatAt(s, p, p.pos.x, p.pos.y + 2.3, p.pos.z, protocol::FloatKind::Block, MessageId::FloatHolyShield, {MsgArg::integer(50)});
    }
    if (t > 0.55) free();
  } else if (k == "purificacaoSagrada") {
    if (t >= 0.35 && !a.done) {
      a.done = true;
      sound(s, p, SoundId::Magic);
      burstAt(s, p, p.pos.x, p.pos.y + 1.2, p.pos.z, 0xffd166, 28, 6);
      forHostiles(s, p, [&](Enemy& e) {
        if (jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z) <= 5.0) {
          const bool isShadow = e.faction == Faction::Shadow;
          HurtOpts o = byPlayer(p);
          o.knock = 2.5;
          o.burstColor = 0xffd166;
          o.big = true;
          hurtEnemy(s, e, isShadow ? 42 : 25, o);
        }
        return true;
      });
    }
    if (t > 0.7) free();
  } else {
    free();
  }
}

// ---------------------------------------------------------------- pulo
void startJump(Sim& s, Player& p, bool boosted, double mx, double mz, double sp, motor::LoadState ls) {
  const auto& M = s.data.balance.movement;
  const double vigorCost = boosted ? M.boost.vigor : M.jump.vigor;
  if (ls != motor::LoadState::Normal) {
    s.toast(p, MessageId::JumpOverloaded, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (p.vigor < vigorCost) {
    s.toast(p, MessageId::JumpNoVigor, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  p.vigor -= vigorCost;
  p.vigorDelay = 0.5;
  p.air = motor::startJump(M, boosted, mx, mz, sp, p.yaw, armorSpeed(s, p));
  p.cmd.reset();
  sound(s, p, SoundId::Dodge);
}

// ---------------------------------------------------------------- comando de clique
std::pair<double, double> followCommand(Sim& s, Player& p) {
  ClickCommand& c = *p.cmd;
  double tx, tz, stop;
  if (c.type == ClickCommand::Type::Attack) {
    Enemy* e = s.enemy(c.enemy);
    if (!e || !e->alive || !sameSpace(p, *e) || jsHypot(e->pos.x - p.pos.x, e->pos.z - p.pos.z) > 60) {
      p.cmd.reset();
      return {0, 0};
    }
    tx = e->pos.x;
    tz = e->pos.z;
    const BasicAttack& b = s.data.weapons[weaponFamily(s, p)].basic;
    // Na demo `b.range` não existe nas magias: a conta dá NaN e o clique de ataque nunca entra em
    // alcance (o personagem só persegue). Mantido para a simulação seguir a demo.
    const double range = b.type == AttackType::Spell ? std::numeric_limits<double>::quiet_NaN() : b.range;
    stop = b.type == AttackType::Ranged ? 16 : range * 0.8 + e->radius;
    p.aimYaw = js::atan2(tx - p.pos.x, tz - p.pos.z);
  } else {
    if (!targetValid(s, p, c.target)) {
      p.cmd.reset();
      return {0, 0};
    }
    const std::optional<Target> t = resolveTarget(s, p, c.target);
    if (!t) {
      p.cmd.reset();
      return {0, 0};
    }
    tx = t->x;
    tz = t->z;
    stop = std::max(1.4, std::min(t->r * 0.8, 4.0));
  }
  const double dx = tx - p.pos.x, dz = tz - p.pos.z, d = jsHypot(dx, dz);
  if (c.type == ClickCommand::Type::Attack) c.inRange = d <= stop;
  if (d <= stop) {
    if (c.type == ClickCommand::Type::Use && p.state == PlayerState::Free) {
      const InteractRef ref = c.target;
      p.cmd.reset();
      runTarget(s, p, ref);
    }
    return {0, 0};
  }
  if (c.type != ClickCommand::Type::Attack) {
    if (d < c.best - 0.3) {
      c.best = d;
      c.since = s.time;
    } else if (s.time - c.since > 1.2) {
      p.cmd.reset();
      return {0, 0};
    }
  }
  return {dx / d, dz / d};
}

}  // namespace

// ---------------------------------------------------------------- criação
Player& createPlayer(Sim& s, const PlayerProfile& profile, std::uint32_t session) {
  auto up = std::make_unique<Player>();
  Player& p = *up;
  p.id = s.newId();
  p.session = session;
  p.name = profile.name;
  p.origin = profile.origin;
  p.start = profile.start;
  p.model = profile.model.empty() ? "paladina" : profile.model;
  StartId start = s.data.starts.find(profile.start);
  if (!start) start = s.data.starts.find("espadachim");
  const StartDef& sd = s.data.starts[start];

  const XZ shelter = s.layout.shelter;
  p.map = MapKind::Region;
  p.pos = {shelter.x, s.mapOf(MapKind::Region).groundHeight(shelter.x, shelter.z), shelter.z};
  p.trainings = {profile.start};
  p.equip.weapon = makeGear(s.uids, sd.weapon, 82);
  p.zone = ZoneKind::Protegida;
  p.place = placeOf(s, MapKind::Region, shelter.x, shelter.z);
  addTo(s.data, s.uids, p.inv, item(s, "pocao"), 2);
  if (profile.start == "artesao") {
    addTo(s.data, s.uids, p.inv, item(s, "minerio"), 4);
    addTo(s.data, s.uids, p.inv, item(s, "madeira"), 2);
  }
  p.mount.id = s.newId();
  s.players.push_back(std::move(up));
  return p;
}

void removePlayer(Sim& s, EntityId id) {
  for (auto it = s.players.begin(); it != s.players.end(); ++it) {
    if ((*it)->id != id) continue;
    Player& p = **it;
    if (p.turb.inside) {
      for (const EntityId e : p.turb.shadows) removeEnemy(s, e);
    }
    s.players.erase(it);
    return;
  }
}

// ---------------------------------------------------------------- utilidades de estado
void stagger(Player& p, double t) {
  if (p.state == PlayerState::Down || p.state == PlayerState::Dead) return;
  p.state = PlayerState::Stagger;
  p.stateT = 0;
  p.staggerT = t;
}

void startChannel(Player& p, MessageId label, double dur, ChannelAction action, std::uint32_t target,
                  bool interruptible, bool anim) {
  Channel c;
  c.label = label;
  c.dur = dur;
  c.t = 0;
  c.interruptible = interruptible;
  c.anim = anim;
  c.action = action;
  c.target = target;
  p.channel = c;
  p.state = PlayerState::Channel;
  p.cmd.reset();  // um clique antigo não deve cancelar a canalização
}

void cancelChannel(Sim& s, Player& p, std::optional<MessageId> reason) {
  if (!p.channel) return;
  const Channel c = *p.channel;
  p.channel.reset();
  if (p.state == PlayerState::Channel) p.state = PlayerState::Free;
  if (reason) s.toast(p, MessageId::ChannelCancelled, {MsgArg::message(c.label), MsgArg::message(*reason)}, ToastKind::Warn);
}

void knockDown(Sim& s, Player& p, const DamageSource& src) {
  if (p.state == PlayerState::Down || p.state == PlayerState::Dead) return;
  cancelChannel(s, p, std::nullopt);
  if (p.mounted) dismount(s, p, true);
  p.state = PlayerState::Down;
  p.stateT = 0;
  p.downSrc = src;
  p.cmd.reset();
  sound(s, p, SoundId::Death);
  s.toast(p, MessageId::PlayerDown, {}, ToastKind::Danger);
  s.toPlayer(p, protocol::EvPlayerDown{});
}

void dismount(Sim& s, Player& p, bool forced) {
  if (!p.mounted) return;
  p.mounted = false;
  p.mount.pos = xz(p.pos);
  p.mount.y = p.pos.y;
  p.mount.yaw = p.yaw;
  const double side = p.yaw + kPi / 2;
  move(s, p, js::sin(side) * 2, js::cos(side) * 2, 0.45);
  if (!forced) s.sfx(p, SoundId::Ui);
}

void mountUp(Sim& s, Player& p) {
  p.mounted = true;
  p.pos.x = p.mount.pos.x;
  p.pos.y = p.mount.y;
  p.pos.z = p.mount.pos.z;
  p.yaw = p.mount.yaw;
  s.sfx(p, SoundId::Ui);
  bookTally(s, p, "montaria", BookCat::Historia, BookDomain::Comercio, "tally.montaria");
}

bool inCombat(const Sim& s, const Player& p) { return s.time - p.lastCombat < 5; }

void drinkPotion(Sim& s, Player& p) {
  if (p.cd.pot > 0) return;
  const ItemId pocao = item(s, "pocao");
  if (!count(p.inv, pocao)) {
    s.toast(p, MessageId::PotionNone, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (p.hp >= p.maxHp) {
    s.toast(p, MessageId::PotionFullHp, {}, ToastKind::Info);
    return;
  }
  removeFrom(p.inv, pocao, 1);
  p.hp = std::min(p.maxHp, p.hp + 45);
  p.cd.pot = 2;
  floatAt(s, p, p.pos.x, p.pos.y + 2.4, p.pos.z, protocol::FloatKind::Heal, MessageId::FloatHeal, {MsgArg::integer(45)});
  s.sfx(p, SoundId::Pickup);
}

void applyAim(Player& p, const protocol::PlayerCommand& cmd) {
  p.aimPoint = {cmd.aimX, cmd.aimY, cmd.aimZ};
  p.aimEnemy = cmd.aimEnemy;
  p.aimYaw = cmd.aimYaw;
  p.aimPitch = cmd.aimPitch;
  p.aimDist = cmd.aimDist;
}

// ---------------------------------------------------------------- o passo
void updatePlayer(Sim& s, Player& p, const PlayerInput& in, double dt) {
  using protocol::Press;
  const StaticMap& map = s.mapOf(p.map);
  const auto& M = s.data.balance.movement;
  const protocol::PlayerCommand& cmd = in.cmd;
  const bool canAct = !p.uiOpen && !p.cinematic;

  p.invuln = std::max(0.0, p.invuln - dt);
  p.cd.q = std::max(0.0, p.cd.q - dt);
  p.cd.e = std::max(0.0, p.cd.e - dt);
  p.cd.pot = std::max(0.0, p.cd.pot - dt);
  p.maxHp = 100 + armorHp(s, p);
  if (p.hp > p.maxHp) p.hp = p.maxHp;

  p.metamorphT = std::max(0.0, p.metamorphT - dt);
  p.shieldT = std::max(0.0, p.shieldT - dt);
  if (p.shieldT <= 0) p.shieldHp = 0;
  if (p.auraVitalT > 0) {
    p.auraVitalT = std::max(0.0, p.auraVitalT - dt);
    p.hp = std::min(p.maxHp, p.hp + 16 * dt);
  }

  if (p.vigorDelay > 0) p.vigorDelay -= dt;
  else p.vigor = std::min(p.maxVigor, p.vigor + 24 * dt);
  if (p.state != PlayerState::Down && p.state != PlayerState::Dead && !inCombat(s, p) && p.hp < p.maxHp)
    p.hp = std::min(p.maxHp, p.hp + (p.zone == ZoneKind::Protegida ? 9 : 3) * dt);

  applyAim(p, cmd);
  const bool lockWanted = canAct && !p.mounted &&
                          (p.state == PlayerState::Free || p.state == PlayerState::Attack || p.state == PlayerState::Skill);
  p.aiming = lockWanted && cmd.has(protocol::kHeldAim);
  p.stateT += dt;

  // intenção de movimento: WASD relativo à câmera, ou aproximação de um alvo clicado
  double mx = 0, mz = 0;
  if (canAct) {
    const double cy = cmd.camYaw;
    const double f = std::clamp<int>(cmd.moveForward, -1, 1);
    const double r = std::clamp<int>(cmd.moveRight, -1, 1);
    mx = -js::sin(cy) * f + js::cos(cy) * r;
    mz = -js::cos(cy) * f - js::sin(cy) * r;
    const double l = jsHypot(mx, mz);
    if (l > 0) {
      mx /= l;
      mz /= l;
      p.cmd.reset();
    } else if (p.cmd) {
      std::tie(mx, mz) = followCommand(s, p);
    }
  }
  const bool moving = mx != 0 || mz != 0;
  p.moving = moving;
  p.moveX = mx;
  p.moveZ = mz;
  const motor::LoadState ls = loadState(s, p);
  const bool shift = cmd.has(protocol::kHeldRun);
  p.crouch = canAct && !p.mounted && !p.air && p.state == PlayerState::Free && cmd.has(protocol::kHeldCrouch);
  p.crouchW = p.crouchW + ((p.crouch ? 1.0 : 0.0) - p.crouchW) * std::min(1.0, dt * 8);
  p.running = !p.aiming && !p.crouch && (shift || (p.cmd && p.cmd->type == ClickCommand::Type::Attack));

  const motor::Wading wade = motor::wadingAt(map, p.pos.x, p.pos.z);
  motor::SpeedInputs si;
  si.running = p.running;
  si.crouch = p.crouch;
  si.aiming = p.aiming;
  si.metamorph = p.metamorphT > 0;
  si.mounted = p.mounted;
  si.wading = wade.wading;
  si.load = ls;
  si.armorSpeed = armorSpeed(s, p);
  const double sp = motor::moveSpeed(M, si, p.landT, dt);

  double v = 0;
  switch (p.state) {
    case PlayerState::Free: {
      if (p.air) {
        XZ pos = xz(p.pos);
        motor::airSteer(M, map, *p.air, pos, moving, mx, mz, sp, dt);
        p.pos.x = pos.x;
        p.pos.z = pos.z;
        v = jsHypot(p.air->vx, p.air->vz);
      } else if (moving) {
        move(s, p, mx * sp * dt, mz * sp * dt, p.mounted ? motor::kMountedRadius : motor::kBodyRadius);
        v = sp;
      }
      const bool fighting = p.aiming || p.aimLock > 0 || (p.cmd && p.cmd->type == ClickCommand::Type::Attack && p.cmd->inRange);
      if (p.mounted) {
        if (moving) p.yaw = lerpAngle(p.yaw, js::atan2(mx, mz), dt * 8);
      } else if (fighting || !moving) {
        p.yaw = lerpAngle(p.yaw, p.aimYaw, dt * 16);
      } else {
        p.yaw = lerpAngle(p.yaw, js::atan2(mx, mz), dt * 12);
      }
      p.aimLock = std::max(0.0, p.aimLock - dt);
      if (in.hit(Press::Potion)) drinkPotion(s, p);
      if (!canAct) break;
      if (p.mounted) {
        const bool attackCmd = p.cmd && p.cmd->type == ClickCommand::Type::Attack;
        if (in.hit(Press::SkillQ) || in.hit(Press::SkillE) || attackCmd) {
          s.toast(p, MessageId::DismountToFight, {}, ToastKind::Info);
          if (attackCmd) p.cmd.reset();
        }
        break;
      }
      if (p.air) break;  // no ar só o movimento
      if (p.aiming && in.hit(Press::Fire)) {
        basicAttack(s, p);
        p.aimLock = 0.6;
      } else if (in.hit(Press::Jump)) {
        startJump(s, p, shift, moving ? mx : 0, moving ? mz : 0, sp, ls);
      } else if (in.hit(Press::Dodge)) {
        if (ls != motor::LoadState::Normal) {
          s.toast(p, MessageId::DodgeOverloaded, {}, ToastKind::Warn);
          s.sfx(p, SoundId::Deny);
        } else if (p.vigor < 22) {
          s.toast(p, MessageId::DodgeNoVigor, {}, ToastKind::Warn);
          s.sfx(p, SoundId::Deny);
        } else {
          p.vigor -= 22;
          p.vigorDelay = 0.6;
          p.state = PlayerState::Dodge;
          p.stateT = 0;
          p.dodgeYaw = moving ? js::atan2(mx, mz) : p.aimYaw;
          p.yaw = p.dodgeYaw;
          p.cmd.reset();
          sound(s, p, SoundId::Dodge);
        }
      } else if (p.cmd && p.cmd->type == ClickCommand::Type::Attack && p.cmd->inRange) {
        basicAttack(s, p);
        p.aimLock = 0.6;
      } else if (in.hit(Press::SkillQ)) {
        useSkill(s, p, 0);
        p.aimLock = 0.6;
      } else if (in.hit(Press::SkillE)) {
        useSkill(s, p, 1);
        p.aimLock = 0.6;
      }
      break;
    }
    case PlayerState::Attack: {
      BasicAct& a = *p.basic;
      if (moving) {
        move(s, p, mx * sp * 0.3 * dt, mz * sp * 0.3 * dt, 0.45);
        v = sp * 0.3;
      }
      if (!a.done && p.stateT >= a.windup) {
        a.done = true;
        resolveBasic(s, p);
      }
      if (p.stateT >= a.windup + a.recover) {
        p.state = PlayerState::Free;
        p.stateT = 0;
      }
      break;
    }
    case PlayerState::Skill: updateSkill(s, p, dt); break;
    case PlayerState::Dodge: {
      if (p.stateT < 0.34) move(s, p, js::sin(p.dodgeYaw) * 14 * dt, js::cos(p.dodgeYaw) * 14 * dt, 0.45);
      p.invuln = p.stateT < 0.28 ? 0.05 : p.invuln;
      if (p.stateT > 0.42) {
        p.state = PlayerState::Free;
        p.stateT = 0;
      }
      break;
    }
    case PlayerState::Stagger:
      if (p.stateT > (p.staggerT != 0 ? p.staggerT : 0.5)) {
        p.state = PlayerState::Free;
        p.stateT = 0;
      }
      break;
    case PlayerState::Channel: {
      if (!p.channel) {
        p.state = PlayerState::Free;
        break;
      }
      if (moving && canAct) {
        cancelChannel(s, p, MessageId::ChannelReasonCancelled);
        break;
      }
      p.channel->t += dt;
      if (p.channel->t >= p.channel->dur) {
        const Channel c = *p.channel;
        p.channel.reset();
        p.state = PlayerState::Free;
        finishChannel(s, p, c);
      }
      break;
    }
    case PlayerState::Down: {
      bool guardNear = false;
      for (const auto& ep : s.enemies) {
        const Enemy& e = *ep;
        if (e.alive && e.faction == Faction::Guard && sameSpace(p, e) && jsHypot(e.pos.x - p.pos.x, e.pos.z - p.pos.z) < 24) {
          guardNear = true;
          break;
        }
      }
      if (guardNear && p.stateT > 2.5) {
        p.state = PlayerState::Free;
        p.hp = jsRound(p.maxHp * 0.35);
        p.invuln = 2.5;
        p.lastCombat = s.time;
        s.toast(p, MessageId::GuardRescued, {}, ToastKind::Info);
        bookTally(s, p, "resgates", BookCat::Feitos, BookDomain::Comunidade, "tally.resgates");
      } else if (p.stateT > 4.5) {
        p.state = PlayerState::Dead;
        p.stateT = 0;
        const protocol::DeathReport report = handleDeath(s, p, p.downSrc);
        if (static_cast<ZoneKind>(report.zone) == ZoneKind::Turbulenta) onTurbulentDeath(s, p, p.downSrc);
        s.toPlayer(p, protocol::EvPlayerDied{report});
      }
      break;
    }
    case PlayerState::Dead: break;
  }
  p.speedNow = v;

  // pulo: a gravidade vale em qualquer estado (derrubado no ar também cai). O mapa é consultado de
  // novo: uma extração ou derrota neste passo pode ter trocado o jogador de mapa.
  motor::applyGravity(M, p.air, p.landT, dt);
  p.pos.y = motor::bodyHeight(s.mapOf(p.map), p.pos.x, p.pos.z, p.air, wade);

  // zona e lugar
  const ZoneKind zone = zoneOf(s, p.map, p.pos.x, p.pos.z);
  if (zone != p.zone) {
    const ZoneKind from = p.zone;
    p.zone = zone;
    s.toPlayer(p, protocol::EvZoneChanged{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(zone)});
  }
  const PlaceId place = placeOf(s, p.map, p.pos.x, p.pos.z);
  if (place != p.place) {
    p.place = place;
    s.toPlayer(p, protocol::EvPlaceChanged{static_cast<std::uint8_t>(place)});
  }
}

}  // namespace rpg::sim
